#ifndef _CONFIG_H_
#define _CONFIG_H_

#define FLASH_2M	(2<<20)
#define FLASH_4M	(4<<20)

/*
 * Cellvision/SparkLAN boards
 */

#if defined(CONFIG_BOARD_CAS630)
#  define CONFIG_BOARD_NAME	"CAS-630"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_CAS670)
#  define CONFIG_BOARD_NAME	"CAS-670"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_CAS700)
#  define CONFIG_BOARD_NAME	"CAS-700"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_CAS790)
#  define CONFIG_BOARD_NAME	"CAS-790"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_CAS771)
#  define CONFIG_BOARD_NAME	"CAS-771"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_CAS861)
#  define CONFIG_BOARD_NAME	"CAS-861"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_NFS101U)
#  define CONFIG_BOARD_NAME	"NFS-101U"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_NFS202U)
#  define CONFIG_BOARD_NAME	"NFS-202U"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

/*
 * Edimax boards
 */
#if defined(CONFIG_BOARD_BR6104K)
#  define CONFIG_BOARD_NAME	"BR-6104K"
#  define CONFIG_FLASH_SIZE	FLASH_2M
#endif

#if defined(CONFIG_BOARD_BR6104KP)
#  define CONFIG_BOARD_NAME	"BR-6104KP"
#  define CONFIG_FLASH_SIZE	FLASH_2M
#endif

/*
 * Infineon boards
 */
#if defined(CONFIG_BOARD_EASY5120PATA)
#  define CONFIG_BOARD_NAME	"EASY 5120P-ATA"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_EASY5120RT)
#  define CONFIG_BOARD_NAME	"EASY 5120-RT"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_EASY5120WVOIP)
#  define CONFIG_BOARD_NAME	"EASY 5120-WVOIP"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_EASY83000)
#  define CONFIG_BOARD_NAME	"EASY 83000"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

/*
 * ZyXEL boards
 */
#if defined(CONFIG_BOARD_P334WT)
#  define CONFIG_BOARD_NAME	"P-334WT"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

#if defined(CONFIG_BOARD_P335)
#  define CONFIG_BOARD_NAME	"P-335"
#  define CONFIG_FLASH_SIZE	FLASH_4M
#endif

/*
 * Default values
 */
#ifndef CONFIG_BOARD_NAME
#  define CONFIG_BOARD_NAME	"ADM5120"
#endif

#ifndef CONFIG_FLASH_SIZE
#  define CONFIG_FLASH_SIZE	FLASH_2M
#endif

#endif /* _CONFIG_H_ */
