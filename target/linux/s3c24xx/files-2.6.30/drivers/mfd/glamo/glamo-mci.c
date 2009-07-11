/*
 *  linux/drivers/mmc/host/glamo-mmc.c - Glamo MMC driver
 *
 *  Copyright (C) 2007 Openmoko, Inc,  Andy Green <andy@openmoko.com>
 *  Based on S3C MMC driver that was:
 *  Copyright (C) 2004-2006 maintech GmbH, Thomas Kleffel <tk@maintech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/crc7.h>

#include <asm/dma.h>
#include <asm/dma-mapping.h>
#include <asm/io.h>

#include "glamo-mci.h"
#include "glamo-core.h"
#include "glamo-regs.h"

#define DRIVER_NAME "glamo-mci"

static void glamo_mci_send_request(struct mmc_host *mmc);
static void glamo_mci_send_command(struct glamo_mci_host *host,
				  struct mmc_command *cmd);

/*
 * Max SD clock rate
 *
 * held at /(3 + 1) due to concerns of 100R recommended series resistor
 * allows 16MHz @ 4-bit --> 8MBytes/sec raw
 *
 * you can override this on kernel commandline using
 *
 *   glamo_mci.sd_max_clk=10000000
 *
 * for example
 */

static int sd_max_clk = 50000000 / 3;
module_param(sd_max_clk, int, 0644);

/*
 * Slow SD clock rate
 *
 * you can override this on kernel commandline using
 *
 *   glamo_mci.sd_slow_ratio=8
 *
 * for example
 *
 * platform callback is used to decide effective clock rate, if not
 * defined then max is used, if defined and returns nonzero, rate is
 * divided by this factor
 */

static int sd_slow_ratio = 8;
module_param(sd_slow_ratio, int, 0644);

/*
 * Post-power SD clock rate
 *
 * you can override this on kernel commandline using
 *
 *   glamo_mci.sd_post_power_clock=1000000
 *
 * for example
 *
 * After changing power to card, clock is held at this rate until first bulk
 * transfer completes
 */

static int sd_post_power_clock = 1000000;
module_param(sd_post_power_clock, int, 0644);


/*
 * SD Signal drive strength
 *
 * you can override this on kernel commandline using
 *
 *   glamo_mci.sd_drive=0
 *
 * for example
 */

static int sd_drive;
module_param(sd_drive, int, 0644);

/*
 * SD allow SD clock to run while idle
 *
 * you can override this on kernel commandline using
 *
 *   glamo_mci.sd_idleclk=0
 *
 * for example
 */

static int sd_idleclk = 0; /* disallow idle clock by default */
module_param(sd_idleclk, int, 0644);

/* used to stash real idleclk state in suspend: we force it to run in there */
static int suspend_sd_idleclk;

static inline void glamo_reg_write(struct glamo_mci_host *glamo,
				u_int16_t reg, u_int16_t val)
{
	writew(val, glamo->mmio_base + reg);
}

static inline u_int16_t glamo_reg_read(struct glamo_mci_host *glamo,
				   u_int16_t reg)
{
	return readw(glamo->mmio_base + reg);
}

static void glamo_reg_set_bit_mask(struct glamo_mci_host *glamo,
				u_int16_t reg, u_int16_t mask,
				u_int16_t val)
{
	u_int16_t tmp;

	val &= mask;

	tmp = glamo_reg_read(glamo, reg);
	tmp &= ~mask;
	tmp |= val;
	glamo_reg_write(glamo, reg, tmp);
}

static void do_pio_read(struct glamo_mci_host *host)
{
	struct scatterlist *sg;
	u16 __iomem *from_ptr = host->data_base;
	struct mmc_data *data = host->mrq->data;
	void *sg_pointer;

	dev_dbg(&host->pdev->dev, "pio_read():\n");
	for (sg = data->sg; sg; sg = sg_next(sg)) {
		sg_pointer = page_address(sg_page(sg)) + sg->offset;


		memcpy(sg_pointer, from_ptr, sg->length);
		from_ptr += sg->length >> 1;

		data->bytes_xfered += sg->length;
	}

	dev_dbg(&host->pdev->dev, "pio_read(): "
	        "complete (no more data).\n");
}

static void do_pio_write(struct glamo_mci_host *host)
{
	struct scatterlist *sg;
	u16 __iomem *to_ptr = host->data_base;
	struct mmc_data *data = host->mrq->data;
	void *sg_pointer;

	dev_dbg(&host->pdev->dev, "pio_write():\n");
	for (sg = data->sg; sg; sg = sg_next(sg)) {
		sg_pointer = page_address(sg_page(sg)) + sg->offset;

		data->bytes_xfered += sg->length;

		memcpy(to_ptr, sg_pointer, sg->length);
		to_ptr += sg->length >> 1;
	}

	dev_dbg(&host->pdev->dev, "pio_write(): complete\n");
}

static void glamo_mci_fix_card_div(struct glamo_mci_host *host, int div)
{
	unsigned long flags;

	spin_lock_irqsave(&host->pdata->pglamo->lock, flags);

	if (div < 0) {
		/* stop clock - remove clock from divider input */
		writew(readw(host->pdata->pglamo->base +
		     GLAMO_REG_CLOCK_GEN5_1) & (~GLAMO_CLOCK_GEN51_EN_DIV_TCLK),
		     host->pdata->pglamo->base + GLAMO_REG_CLOCK_GEN5_1);
	} else {

		if (host->force_slow_during_powerup)
			div = host->clk_rate / sd_post_power_clock;
		else if (host->pdata->glamo_mci_use_slow &&
		         host->pdata->glamo_mci_use_slow())
			div = div * sd_slow_ratio;

		if (div > 255)
			div = 255;
		/*
		 * set the nearest prescaler factor
		 *
		 * register shared with SCLK divisor -- no chance of race because
		 * we don't use sensor interface
		 */
		writew((readw(host->pdata->pglamo->base +
		              GLAMO_REG_CLOCK_GEN8) & 0xff00) | div,
		       host->pdata->pglamo->base + GLAMO_REG_CLOCK_GEN8);
		/* enable clock to divider input */
		writew(readw(host->pdata->pglamo->base +
		             GLAMO_REG_CLOCK_GEN5_1) | GLAMO_CLOCK_GEN51_EN_DIV_TCLK,
		       host->pdata->pglamo->base + GLAMO_REG_CLOCK_GEN5_1);
		mdelay(5);
	}
	spin_unlock_irqrestore(&host->pdata->pglamo->lock, flags);
}

static int glamo_mci_set_card_clock(struct glamo_mci_host *host, int freq)
{
	int div = 0;
	int real_rate = 0;

	if (freq) {
		/* Set clock */
		for (div = 0; div < 255; div++) {
			real_rate = host->clk_rate / (div + 1);
			if (real_rate <= freq)
				break;
		}

		host->clk_div = div;
		glamo_mci_fix_card_div(host, div);
	} else {
		/* stop clock */
		host->clk_div = 0xff;

		if (!sd_idleclk && !host->force_slow_during_powerup)
			/* clock off */
			glamo_mci_fix_card_div(host, -1);
	}
	host->real_rate = real_rate;
	return real_rate;
}


static void glamo_mci_irq_worker(struct work_struct *work)
{
	struct glamo_mci_host *host =
			    container_of(work, struct glamo_mci_host, irq_work);
	struct mmc_command *cmd = host->mrq->cmd;

	if (cmd->data->flags & MMC_DATA_READ) {
		do_pio_read(host);
	}

	/* issue STOP if we have been given one to use */
	if (host->mrq->stop) {
		glamo_mci_send_command(host, host->mrq->stop);
	}

	if (!sd_idleclk && !host->force_slow_during_powerup)
		/* clock off */
		glamo_mci_fix_card_div(host, -1);

	host->mrq = NULL;
	mmc_request_done(host->mmc, cmd->mrq);
}

static irqreturn_t glamo_mci_irq(int irq, void *devid)
{
	struct glamo_mci_host *host = (struct glamo_mci_host*)devid;
	u16 status;
	struct mmc_command *cmd;
	unsigned long flags;

	if (host->suspending) { /* bad news, dangerous time */
		dev_err(&host->pdev->dev, "****glamo_mci_irq before resumed\n");
		goto leave;
	}

	if (!host->mrq)
		goto leave;
	cmd = host->mrq->cmd;
	if (!cmd)
		goto leave;

	spin_lock_irqsave(&host->lock, flags);

	status = readw(host->mmio_base + GLAMO_REG_MMC_RB_STAT1);
	dev_dbg(&host->pdev->dev, "status = 0x%04x\n", status);

	/* we ignore a data timeout report if we are also told the data came */
	if (status & GLAMO_STAT1_MMC_RB_DRDY)
		status &= ~GLAMO_STAT1_MMC_DTOUT;

	if (status & (GLAMO_STAT1_MMC_RTOUT |
		      GLAMO_STAT1_MMC_DTOUT))
		cmd->error = -ETIMEDOUT;
	if (status & (GLAMO_STAT1_MMC_BWERR |
		      GLAMO_STAT1_MMC_BRERR))
		cmd->error = -EILSEQ;
	if (cmd->error) {
		dev_info(&host->pdev->dev, "Error after cmd: 0x%x\n", status);
		goto done;
	}

	/*
	 * disable the initial slow start after first bulk transfer
	 */
	if (host->force_slow_during_powerup)
		host->force_slow_during_powerup--;

	/*
	 * we perform the memcpy out of Glamo memory outside of IRQ context
	 * so we don't block other interrupts
	 */
	schedule_work(&host->irq_work);

	goto unlock;

done:
	host->mrq = NULL;
	mmc_request_done(host->mmc, cmd->mrq);
unlock:
	spin_unlock_irqrestore(&host->lock, flags);
leave:
	return IRQ_HANDLED;
}

static void glamo_mci_send_command(struct glamo_mci_host *host,
				  struct mmc_command *cmd)
{
	u8 u8a[6];
	u16 fire = 0;
	unsigned int timeout = 1000000;
	u16 * reg_resp = (u16 *)(host->mmio_base + GLAMO_REG_MMC_CMD_RSP1);
	u16 status;

	/* if we can't do it, reject as busy */
	if (!readw(host->mmio_base + GLAMO_REG_MMC_RB_STAT1) &
	     GLAMO_STAT1_MMC_IDLE) {
		host->mrq = NULL;
		cmd->error = -EBUSY;
		mmc_request_done(host->mmc, host->mrq);
		return;
	}

	/* create an array in wire order for CRC computation */
	u8a[0] = 0x40 | (cmd->opcode & 0x3f);
	u8a[1] = (u8)(cmd->arg >> 24);
	u8a[2] = (u8)(cmd->arg >> 16);
	u8a[3] = (u8)(cmd->arg >> 8);
	u8a[4] = (u8)cmd->arg;
	u8a[5] = (crc7(0, u8a, 5) << 1) | 0x01; /* crc7 on first 5 bytes of packet */

	/* issue the wire-order array including CRC in register order */
	writew((u8a[4] << 8) | u8a[5], host->mmio_base + GLAMO_REG_MMC_CMD_REG1);
	writew((u8a[2] << 8) | u8a[3], host->mmio_base + GLAMO_REG_MMC_CMD_REG2);
	writew((u8a[0] << 8) | u8a[1], host->mmio_base + GLAMO_REG_MMC_CMD_REG3);

	/* command index toggle */
	fire |= (host->request_counter & 1) << 12;

	/* set type of command */
	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_BC:
		fire |= GLAMO_FIRE_MMC_CMDT_BNR;
		break;
	case MMC_CMD_BCR:
		fire |= GLAMO_FIRE_MMC_CMDT_BR;
		break;
	case MMC_CMD_AC:
		fire |= GLAMO_FIRE_MMC_CMDT_AND;
		break;
	case MMC_CMD_ADTC:
		fire |= GLAMO_FIRE_MMC_CMDT_AD;
		break;
	}
	/*
	 * if it expects a response, set the type expected
	 *
	 * R1, Length  : 48bit, Normal response
	 * R1b, Length : 48bit, same R1, but added card busy status
	 * R2, Length  : 136bit (really 128 bits with CRC snipped)
	 * R3, Length  : 48bit (OCR register value)
	 * R4, Length  : 48bit, SDIO_OP_CONDITION, Reverse SDIO Card
	 * R5, Length  : 48bit, IO_RW_DIRECTION, Reverse SDIO Card
	 * R6, Length  : 48bit (RCA register)
	 * R7, Length  : 48bit (interface condition, VHS(voltage supplied),
	 *                     check pattern, CRC7)
	 */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1: /* same index as R6 and R7 */
		fire |= GLAMO_FIRE_MMC_RSPT_R1;
		break;
	case MMC_RSP_R1B:
		fire |= GLAMO_FIRE_MMC_RSPT_R1b;
		break;
	case MMC_RSP_R2:
		fire |= GLAMO_FIRE_MMC_RSPT_R2;
		break;
	case MMC_RSP_R3:
		fire |= GLAMO_FIRE_MMC_RSPT_R3;
		break;
	/* R4 and R5 supported by chip not defined in linux/mmc/core.h (sdio) */
	}
	/*
	 * From the command index, set up the command class in the host ctrllr
	 *
	 * missing guys present on chip but couldn't figure out how to use yet:
	 *     0x0 "stream read"
	 *     0x9 "cancel running command"
	 */
	switch (cmd->opcode) {
	case MMC_READ_SINGLE_BLOCK:
		fire |= GLAMO_FIRE_MMC_CC_SBR; /* single block read */
		break;
	case MMC_SWITCH: /* 64 byte payload */
	case SD_APP_SEND_SCR:
	case MMC_READ_MULTIPLE_BLOCK:
		/* we will get an interrupt off this */
		if (!cmd->mrq->stop)
			/* multiblock no stop */
			fire |= GLAMO_FIRE_MMC_CC_MBRNS;
		else
			 /* multiblock with stop */
			fire |= GLAMO_FIRE_MMC_CC_MBRS;
		break;
	case MMC_WRITE_BLOCK:
		fire |= GLAMO_FIRE_MMC_CC_SBW; /* single block write */
		break;
	case MMC_WRITE_MULTIPLE_BLOCK:
		if (cmd->mrq->stop)
			 /* multiblock with stop */
			fire |= GLAMO_FIRE_MMC_CC_MBWS;
		else
 			 /* multiblock NO stop-- 'RESERVED'? */
			fire |= GLAMO_FIRE_MMC_CC_MBWNS;
		break;
	case MMC_STOP_TRANSMISSION:
		fire |= GLAMO_FIRE_MMC_CC_STOP; /* STOP */
		break;
	default:
		fire |= GLAMO_FIRE_MMC_CC_BASIC; /* "basic command" */
		break;
	}
	/* always largest timeout */
	writew(0xfff, host->mmio_base + GLAMO_REG_MMC_TIMEOUT);

	/* Generate interrupt on txfer */
	glamo_reg_set_bit_mask(host, GLAMO_REG_MMC_BASIC, ~0x3e,
		   0x0800 | GLAMO_BASIC_MMC_NO_CLK_RD_WAIT |
		   GLAMO_BASIC_MMC_EN_COMPL_INT | (sd_drive << 6));

	/* send the command out on the wire */
	/* dev_info(&host->pdev->dev, "Using FIRE %04X\n", fire); */
	writew(fire, host->mmio_base + GLAMO_REG_MMC_CMD_FIRE);

	/* we are deselecting card?  because it isn't going to ack then... */
	if ((cmd->opcode == 7) && (cmd->arg == 0))
		return;

	/*
	 * we must spin until response is ready or timed out
	 * -- we don't get interrupts unless there is a bulk rx
	 */
	udelay(5);
	do
		status = readw(host->mmio_base + GLAMO_REG_MMC_RB_STAT1);
	while (((((status >> 15) & 1) != (host->request_counter & 1)) ||
		(!(status & (GLAMO_STAT1_MMC_RB_RRDY |
			     GLAMO_STAT1_MMC_RTOUT |
			     GLAMO_STAT1_MMC_DTOUT |
			     GLAMO_STAT1_MMC_BWERR |
			     GLAMO_STAT1_MMC_BRERR)))) && (timeout--));

	if ((status & (GLAMO_STAT1_MMC_RTOUT |
				   GLAMO_STAT1_MMC_DTOUT)) ||
		(timeout == 0)) {
		cmd->error = -ETIMEDOUT;
	} else if (status & (GLAMO_STAT1_MMC_BWERR |
			  GLAMO_STAT1_MMC_BRERR)) {
		cmd->error = -EILSEQ;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = readw(&reg_resp[0]) |
			               (readw(&reg_resp[1]) << 16);
			cmd->resp[2] = readw(&reg_resp[2]) |
			               (readw(&reg_resp[3]) << 16);
			cmd->resp[1] = readw(&reg_resp[4]) |
			               (readw(&reg_resp[5]) << 16);
			cmd->resp[0] = readw(&reg_resp[6]) |
			               (readw(&reg_resp[7]) << 16);
		} else {
			cmd->resp[0] = (readw(&reg_resp[0]) >> 8) |
			               (readw(&reg_resp[1]) << 8) |
			               ((readw(&reg_resp[2])) << 24);
		}
	}
}

static int glamo_mci_prepare_pio(struct glamo_mci_host *host,
				 struct mmc_data *data)
{
	/* set up the block info */
	writew(data->blksz, host->mmio_base + GLAMO_REG_MMC_DATBLKLEN);
	writew(data->blocks, host->mmio_base + GLAMO_REG_MMC_DATBLKCNT);
	dev_dbg(&host->pdev->dev, "(blksz=%d, count=%d)\n",
				   data->blksz, data->blocks);
	data->bytes_xfered = 0;

	/* if write, prep the write into the shared RAM before the command */
	if (data->flags & MMC_DATA_WRITE) {
		do_pio_write(host);
	}
	return 0;
}

static void glamo_mci_send_request(struct mmc_host *mmc)
{
	struct glamo_mci_host *host = mmc_priv(mmc);
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = mrq->cmd;
	int timeout = 1000000;

    host->request_counter++;
	/* this guy has data to read/write? */
	if (cmd->data) {
		if(glamo_mci_prepare_pio(host, cmd->data)) {
			cmd->data->error = -EIO;
			goto done;
		}
	}

	dev_dbg(&host->pdev->dev,"cmd 0x%x, "
		 "arg 0x%x data=%p mrq->stop=%p flags 0x%x\n",
		 cmd->opcode, cmd->arg, cmd->data, cmd->mrq->stop,
		 cmd->flags);

	/* resume requested clock rate
	 * scale it down by sd_slow_ratio if platform requests it
	 */
	glamo_mci_fix_card_div(host, host->clk_div);

	glamo_mci_send_command(host, cmd);

	/*
	 * if we don't have bulk data to take care of, we're done
	 */
	if (!cmd->data || cmd->error)
		goto done;

	/*
	 * Otherwise can can use the interrupt as async completion --
	 * if there is read data coming, or we wait for write data to complete,
	 * exit without mmc_request_done() as the payload interrupt
	 * will service it
	 */
	dev_dbg(&host->pdev->dev, "Waiting for payload data\n");
	/*
	 * if the glamo INT# line isn't wired (*cough* it can happen)
	 * I'm afraid we have to spin on the IRQ status bit and "be
	 * our own INT# line"
	 */
	if (!host->pdata->pglamo->irq_works) {
		/*
		 * we have faith we will get an "interrupt"...
		 * but something insane like suspend problems can mean
		 * we spin here forever, so we timeout after a LONG time
		 */
		while ((!(readw(host->pdata->pglamo->base +
			 GLAMO_REG_IRQ_STATUS) & GLAMO_IRQ_MMC)) &&
		       (timeout--));

		if (timeout < 0) {
			if (cmd->data->error)
				cmd->data->error = -ETIMEDOUT;
			dev_err(&host->pdev->dev, "Payload timeout\n");
			goto bail;
		}
		/* ack this interrupt source */
		writew(GLAMO_IRQ_MMC, host->pdata->pglamo->base +
		       GLAMO_REG_IRQ_CLEAR);

		/* yay we are an interrupt controller! -- call the ISR
		 * it will stop clock to card
		 */
		glamo_mci_irq(IRQ_GLAMO(GLAMO_IRQIDX_MMC), host);
	}
    return;
done:
	host->mrq = NULL;
	mmc_request_done(host->mmc, cmd->mrq);
bail:
	if (!sd_idleclk && !host->force_slow_during_powerup)
		/* stop the clock to card */
		glamo_mci_fix_card_div(host, -1);
}

static void glamo_mci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct glamo_mci_host *host = mmc_priv(mmc);

	host->mrq = mrq;
	glamo_mci_send_request(mmc);
}

static void glamo_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct glamo_mci_host *host = mmc_priv(mmc);
	int bus_width = 0;
	int powering = 0;

	if (host->suspending) {
		dev_err(&host->pdev->dev, "IGNORING glamo_mci_set_ios while "
								 "suspended\n");
		return;
	}

	/* Set power */
	switch(ios->power_mode) {
	case MMC_POWER_UP:
        mmc_regulator_set_ocr(host->regulator, ios->vdd);
		host->vdd_current = ios->vdd;
		break;
	case MMC_POWER_ON:
		/*
		 * we should use very slow clock until first bulk
		 * transfer completes OK
		 */
		host->force_slow_during_powerup = 1;

		if (host->vdd_current != ios->vdd) {
            mmc_regulator_set_ocr(host->regulator, ios->vdd);
			host->vdd_current = ios->vdd;
		}
		if (host->power_mode_current == MMC_POWER_OFF) {
			glamo_engine_enable(host->pdata->pglamo,
							      GLAMO_ENGINE_MMC);
			powering = 1;
		}
		break;

	case MMC_POWER_OFF:
	default:
		if (host->power_mode_current == MMC_POWER_OFF)
			break;
		/* never want clocking with dead card */
		glamo_mci_fix_card_div(host, -1);

		glamo_engine_disable(host->pdata->pglamo,
				     GLAMO_ENGINE_MMC);

		mmc_regulator_set_ocr(host->regulator, 0);
		host->vdd_current = -1;
		break;
	}
	host->power_mode_current = ios->power_mode;

	glamo_mci_set_card_clock(host, ios->clock);

	/* after power-up, we are meant to give it >= 74 clocks so it can
	 * initialize itself.  Doubt any modern cards need it but anyway...
	 */
	if (powering)
		mdelay(1);

	if (!sd_idleclk && !host->force_slow_during_powerup)
		/* stop the clock to card, because we are idle until transfer */
		glamo_mci_fix_card_div(host, -1);

	if ((ios->power_mode == MMC_POWER_ON) ||
	    (ios->power_mode == MMC_POWER_UP)) {
		dev_info(&host->pdev->dev,
			"powered (vdd = %d) clk: %lukHz div=%d (req: %ukHz). "
			"Bus width=%d\n",(int)ios->vdd,
			host->real_rate / 1000, (int)host->clk_div,
			ios->clock / 1000, (int)ios->bus_width);
	} else
		dev_info(&host->pdev->dev, "glamo_mci_set_ios: power down.\n");

	/* set bus width */
	if (ios->bus_width == MMC_BUS_WIDTH_4)
		bus_width = GLAMO_BASIC_MMC_EN_4BIT_DATA;
	glamo_reg_set_bit_mask(host, GLAMO_REG_MMC_BASIC,
					       GLAMO_BASIC_MMC_EN_4BIT_DATA |
					       GLAMO_BASIC_MMC_EN_DR_STR0 |
					       GLAMO_BASIC_MMC_EN_DR_STR1,
						   bus_width | sd_drive << 6);
}


/*
 * no physical write protect supported by us
 */
static int glamo_mci_get_ro(struct mmc_host *mmc)
{
	return 0;
}

static struct mmc_host_ops glamo_mci_ops = {
	.request	= glamo_mci_request,
	.set_ios	= glamo_mci_set_ios,
	.get_ro		= glamo_mci_get_ro,
};

static int glamo_mci_probe(struct platform_device *pdev)
{
	struct mmc_host 	*mmc;
	struct glamo_mci_host 	*host;
	int ret;

	dev_info(&pdev->dev, "glamo_mci driver (C)2007 Openmoko, Inc\n");

	mmc = mmc_alloc_host(sizeof(struct glamo_mci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto probe_out;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = pdev;
	host->pdata = pdev->dev.platform_data;
	host->power_mode_current = MMC_POWER_OFF;

	spin_lock_init(&host->lock);
	INIT_WORK(&host->irq_work, glamo_mci_irq_worker);

	host->regulator = regulator_get(&pdev->dev, "SD_3V3");
	if (!host->regulator) {
		dev_err(&pdev->dev, "Cannot proceed without regulator.\n");
		ret = -ENODEV;
		goto probe_free_host;
	}

	host->mmio_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!host->mmio_mem) {
		dev_err(&pdev->dev,
			"failed to get io memory region resouce.\n");
		ret = -ENOENT;
		goto probe_regulator_put;
	}

	host->mmio_mem = request_mem_region(host->mmio_mem->start,
	                                    resource_size(host->mmio_mem),
	                                    pdev->name);

	if (!host->mmio_mem) {
		dev_err(&pdev->dev, "failed to request io memory region.\n");
		ret = -ENOENT;
		goto probe_regulator_put;
	}

	host->mmio_base = ioremap(host->mmio_mem->start,
	                          resource_size(host->mmio_mem));
	if (!host->mmio_base) {
		dev_err(&pdev->dev, "failed to ioremap() io memory region.\n");
		ret = -EINVAL;
		goto probe_free_mem_region_mmio;
	}


	/* Get ahold of our data buffer we use for data in and out on MMC */
	host->data_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!host->data_mem) {
		dev_err(&pdev->dev,
			"failed to get io memory region resource.\n");
		ret = -ENOENT;
		goto probe_iounmap_mmio;
	}

	host->data_mem = request_mem_region(host->data_mem->start,
	                                    resource_size(host->data_mem),
										pdev->name);

	if (!host->data_mem) {
		dev_err(&pdev->dev, "failed to request io memory region.\n");
		ret = -ENOENT;
		goto probe_iounmap_mmio;
	}
	host->data_base = ioremap(host->data_mem->start,
	                          resource_size(host->data_mem));

	if (host->data_base == 0) {
		dev_err(&pdev->dev, "failed to ioremap() io memory region.\n");
		ret = -EINVAL;
		goto probe_free_mem_region_data;
	}

	ret = request_irq(IRQ_GLAMO(GLAMO_IRQIDX_MMC), glamo_mci_irq, IRQF_SHARED,
	               pdev->name, host);
    if (ret) {
		dev_err(&pdev->dev, "failed to register irq.\n");
		goto probe_iounmap_data;
	}


	host->vdd_current = -1;
	host->clk_rate = 50000000; /* really it's 49152000 */
	host->clk_div = 16;

	/* explain our host controller capabilities */
	mmc->ops       = &glamo_mci_ops;
	mmc->ocr_avail = mmc_regulator_get_ocrmask(host->regulator);
	mmc->caps      = MMC_CAP_4_BIT_DATA |
	                 MMC_CAP_MMC_HIGHSPEED |
	                 MMC_CAP_SD_HIGHSPEED;
	mmc->f_min     = host->clk_rate / 256;
	mmc->f_max     = sd_max_clk;

	mmc->max_blk_count = (1 << 16) - 1; /* GLAMO_REG_MMC_RB_BLKCNT */
	mmc->max_blk_size  = (1 << 12) - 1; /* GLAMO_REG_MMC_RB_BLKLEN */
	mmc->max_req_size  = resource_size(host->data_mem);
	mmc->max_seg_size  = mmc->max_req_size;
	mmc->max_phys_segs = 128;
	mmc->max_hw_segs   = 128;

	platform_set_drvdata(pdev, mmc);

	glamo_engine_enable(host->pdata->pglamo, GLAMO_ENGINE_MMC);
    glamo_engine_reset(host->pdata->pglamo, GLAMO_ENGINE_MMC);

	if ((ret = mmc_add_host(mmc))) {
		dev_err(&pdev->dev, "failed to add mmc host.\n");
		goto probe_freeirq;
	}

	writew((u16)(host->data_mem->start),
	       host->mmio_base + GLAMO_REG_MMC_WDATADS1);
	writew((u16)((host->data_mem->start) >> 16),
	       host->mmio_base + GLAMO_REG_MMC_WDATADS2);

	writew((u16)host->data_mem->start, host->mmio_base +
	       GLAMO_REG_MMC_RDATADS1);
	writew((u16)(host->data_mem->start >> 16), host->mmio_base +
	       GLAMO_REG_MMC_RDATADS2);

	dev_info(&pdev->dev,"initialisation done.\n");
	return 0;

probe_freeirq:
	free_irq(IRQ_GLAMO(GLAMO_IRQIDX_MMC), host);
probe_iounmap_data:
    iounmap(host->data_base);
probe_free_mem_region_data:
	release_mem_region(host->data_mem->start, resource_size(host->data_mem));
probe_iounmap_mmio:
	iounmap(host->mmio_base);
probe_free_mem_region_mmio:
	release_mem_region(host->mmio_mem->start, resource_size(host->mmio_mem));
probe_regulator_put:
    regulator_put(host->regulator);
probe_free_host:
	mmc_free_host(mmc);
probe_out:
	return ret;
}

static int glamo_mci_remove(struct platform_device *pdev)
{
	struct mmc_host	*mmc = platform_get_drvdata(pdev);
	struct glamo_mci_host *host = mmc_priv(mmc);

	free_irq(IRQ_GLAMO(GLAMO_IRQIDX_MMC), host);

	mmc_remove_host(mmc);
	iounmap(host->mmio_base);
	iounmap(host->data_base);
	release_mem_region(host->mmio_mem->start, resource_size(host->mmio_mem));
	release_mem_region(host->data_mem->start, resource_size(host->data_mem));

	regulator_put(host->regulator);

	mmc_free_host(mmc);

	glamo_engine_disable(host->pdata->pglamo, GLAMO_ENGINE_MMC);
	return 0;
}


#ifdef CONFIG_PM

static int glamo_mci_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	struct glamo_mci_host 	*host = mmc_priv(mmc);
	int ret;

	cancel_work_sync(&host->irq_work);

	/*
	 * possible workaround for SD corruption during suspend - resume
	 * make sure the clock was running during suspend and consequently
	 * resume
	 */
	glamo_mci_fix_card_div(host, host->clk_div);

	/* we are going to do more commands to override this in
	 * mmc_suspend_host(), so we need to change sd_idleclk for the
	 * duration as well
	 */
	suspend_sd_idleclk = sd_idleclk;
	sd_idleclk = 1;

	ret = mmc_suspend_host(mmc, state);

	host->suspending++;

	return ret;
}

int glamo_mci_resume(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	struct glamo_mci_host 	*host = mmc_priv(mmc);
	int ret;

	sd_idleclk = 1;

	glamo_engine_enable(host->pdata->pglamo, GLAMO_ENGINE_MMC);
	glamo_engine_reset(host->pdata->pglamo, GLAMO_ENGINE_MMC);

	host->suspending--;

	ret = mmc_resume_host(mmc);

	/* put sd_idleclk back to pre-suspend state */
	sd_idleclk = suspend_sd_idleclk;

	return ret;
}
EXPORT_SYMBOL_GPL(glamo_mci_resume);

#else /* CONFIG_PM */
#define glamo_mci_suspend NULL
#define glamo_mci_resume NULL
#endif /* CONFIG_PM */


static struct platform_driver glamo_mci_driver =
{
	.driver.name	= "glamo-mci",
	.probe		= glamo_mci_probe,
	.remove		= glamo_mci_remove,
	.suspend	= glamo_mci_suspend,
	.resume		= glamo_mci_resume,
};

static int __init glamo_mci_init(void)
{
	platform_driver_register(&glamo_mci_driver);
	return 0;
}

static void __exit glamo_mci_exit(void)
{
	platform_driver_unregister(&glamo_mci_driver);
}

module_init(glamo_mci_init);
module_exit(glamo_mci_exit);

MODULE_DESCRIPTION("Glamo MMC/SD Card Interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andy Green <andy@openmoko.com>");
