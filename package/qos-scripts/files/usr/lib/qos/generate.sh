#!/bin/sh
[ -e /etc/functions.sh ] && . /etc/functions.sh || . ./functions.sh
[ -x /sbin/modprobe ] && {
	insmod="modprobe"
	rmmod="$insmod -r"
} || {
	insmod="insmod"
	rmmod="rmmod"
}

add_insmod() {
	eval "export isset=\${insmod_$1}"
	case "$isset" in
		1) ;;
		*) {
			[ "$2" ] && append INSMOD "$rmmod $1 >&- 2>&-" "$N"
			append INSMOD "$insmod $* >&- 2>&-" "$N"; export insmod_$1=1
		};;
	esac
}

[ -e /etc/config/network ] && {
	# only try to parse network config on openwrt

	find_ifname() {(
		reset_cb
		include /lib/network
		scan_interfaces
		config_get "$1" ifname
	)}
} || {
	find_ifname() {
		echo "Interface not found."
		exit 1
	}
}

parse_matching_rule() {
	local var="$1"
	local section="$2"
	local options="$3"
	local prefix="$4"
	local suffix="$5"
	local proto="$6"
	local mport=""
	local ports=""

	append "$var" "$prefix" "$N"
	for option in $options; do
		case "$option" in
			proto) config_get value "$section" proto; proto="${proto:-$value}";;
		esac
	done
	config_get type "$section" TYPE
	case "$type" in
		classify) unset pkt; append "$var" "-m mark --mark 0";;
		default) pkt=1; append "$var" "-m mark --mark 0";;
		reclassify) pkt=1;;
	esac
	append "$var" "${proto:+-p $proto}"
	for option in $options; do
		config_get value "$section" "$option"
		
		case "$pkt:$option" in
			*:srchost)
				append "$var" "-s $value"
			;;
			*:dsthost)
				append "$var" "-d $value"
			;;
			*:layer7)
				add_insmod ipt_layer7
				add_insmod xt_layer7
				append "$var" "-m layer7 --l7proto $value${pkt:+ --l7pkt}"
			;;
			*:ports|*:srcports|*:dstports)
				value="$(echo "$value" | sed -e 's,-,:,g')"
				lproto=${lproto:-tcp}
				case "$proto" in
					""|tcp|udp) append "$var" "-m ${proto:-tcp -p tcp} -m multiport";;
					*) unset "$var"; return 0;;
				esac
				case "$option" in
					ports)
						config_set "$section" srcports ""
						config_set "$section" dstports ""
						config_set "$section" portrange ""
						append "$var" "--ports $value"
					;;
					srcports)
						config_set "$section" ports ""
						config_set "$section" dstports ""
						config_set "$section" portrange ""
						append "$var" "--sports $value"
					;;
					dstports)
						config_set "$section" ports ""
						config_set "$section" srcports ""
						config_set "$section" portrange ""
						append "$var" "--dports $value"
					;;
				esac
				ports=1
			;;
			*:portrange)
				config_set "$section" ports ""
				config_set "$section" srcports ""
				config_set "$section" dstports ""
				value="$(echo "$value" | sed -e 's,-,:,g')"
				case "$proto" in
					""|tcp|udp) append "$var" "-m ${proto:-tcp -p tcp} --sport $value --dport $value";;
					*) unset "$var"; return 0;;
				esac
				ports=1
			;;
			*:connbytes)
				value="$(echo "$value" | sed -e 's,-,:,g')"
				add_insmod ipt_connbytes
				append "$var" "-m connbytes --connbytes $value --connbytes-dir both --connbytes-mode bytes"
			;;
			*:tos)
                                add_insmod ipt_tos
                                case "$value" in
                                        !*) append "$var" "-m tos ! --tos $value";;
                                        *) append "$var" "-m tos --tos $value"
                                esac
                        ;;
			*:dscp)
                                add_insmod ipt_dscp
				dscp_option="--dscp"
                                [ -z "${value%%[EBCA]*}" ] && dscp_option="--dscp-class"
				case "$value" in
                                       	!*) append "$var" "-m dscp ! $dscp_option $value";;
                                       	*) append "$var" "-m dscp $dscp_option $value"
                                esac
                        ;;
			*:direction)
				value="$(echo "$value" | sed -e 's,-,:,g')"
				if [ "$value" = "out" ]; then
					append "$var" "-o $device"
				elif [ "$value" = "in" ]; then
					append "$var" "-i $device"
				fi
			;;
			1:pktsize)
				value="$(echo "$value" | sed -e 's,-,:,g')"
				add_insmod ipt_length
				append "$var" "-m length --length $value"
			;;
			1:limit)
				add_insmod ipt_limit
				append "$var" "-m limit --limit $value"
			;;
			1:tcpflags)
				case "$proto" in
					tcp) append "$var" "-m tcp --tcp-flags ALL $value";;
					*) unset $var; return 0;;
				esac
			;;
			1:mark)
				config_get class "${value##!}" classnr
				[ -z "$class" ] && continue;
				case "$value" in
					!*) append "$var" "-m mark ! --mark $class";;
					*) append "$var" "-m mark --mark $class";;
				esac
			;;
			1:TOS)
				add_insmod ipt_TOS
				config_get TOS "$rule" 'TOS'
				suffix="-j TOS --set-tos "${TOS:-"Normal-Service"}
			;;
			1:DSCP)
				add_insmod ipt_DSCP
				config_get DSCP "$rule" 'DSCP'
				[ -z "${DSCP%%[EBCA]*}" ] && set_value="--set-dscp-class $DSCP" \
				|| set_value="--set-dscp $DSCP"
				suffix="-j DSCP $set_value"
			;;
		esac
	done
	append "$var" "$suffix"
	case "$ports:$proto" in
		1:)	parse_matching_rule "$var" "$section" "$options" "$prefix" "$suffix" "udp";;
	esac
}

config_cb() {
	option_cb() {
		return 0
	}

	# Section start
	case "$1" in
		interface)
			config_set "$2" "classgroup" "Default"
			config_set "$2" "upload" "128"
		;;
		classify|default|reclassify)
			option_cb() {
				append options "$1"
			}
		;;
	esac

    # Section end
	config_get TYPE "$CONFIG_SECTION" TYPE
	case "$TYPE" in
		interface)
			config_get_bool enabled "$CONFIG_SECTION" enabled 1
			[ 1 -eq "$enabled" ] || return 0
			config_get classgroup "$CONFIG_SECTION" classgroup
			config_set "$CONFIG_SECTION" imqdev "$C"
			C=$(($C+1))
			append INTERFACES "$CONFIG_SECTION"
			config_set "$classgroup" enabled 1
			config_get device "$CONFIG_SECTION" device
			[ -z "$device" ] && {
				device="$(find_ifname ${CONFIG_SECTION})"
				config_set "$CONFIG_SECTION" device "${device:-eth0}"
			}
		;;
		classgroup) append CG "$CONFIG_SECTION";;
		classify|default|reclassify)
			case "$TYPE" in
				classify) var="ctrules";;
				*) var="rules";;
			esac
			config_get target "$CONFIG_SECTION" target
			config_set "$CONFIG_SECTION" options "$options"
			append "$var" "$CONFIG_SECTION"
			unset options
		;;
	esac
}


enum_classes() {
	local c="0"
	config_get classes "$1" classes
	config_get default "$1" default
	for class in $classes; do
		c="$(($c + 1))"
		config_set "${class}" classnr $c
		case "$class" in
			$default) class_default=$c;;
		esac
	done
	class_default="${class_default:-$c}"
}

cls_var() {
	local varname="$1"
	local class="$2"
	local name="$3"
	local type="$4"
	local default="$5"
	local tmp tmp1 tmp2
	config_get tmp1 "$class" "$name"
	config_get tmp2 "${class}_${type}" "$name"
	tmp="${tmp2:-$tmp1}"
	tmp="${tmp:-$tmp2}"
	export ${varname}="${tmp:-$default}"
}

tcrules() {
	dir=/usr/lib/qos
	[ -e $dir/tcrules.awk ] || dir=.
	echo "$cstr" | awk \
		-v device="$dev" \
		-v linespeed="$rate" \
		-f $dir/tcrules.awk
}

start_interface() {
	local iface="$1"
	local num_imq="$2"
	config_get device "$iface" device
	config_get_bool enabled "$iface" enabled 1
	[ -z "$device" -o 1 -ne "$enabled" ] && {
		return 1 
	}
	config_get upload "$iface" upload
	config_get halfduplex "$iface" halfduplex
	config_get download "$iface" download
	config_get classgroup "$iface" classgroup
	config_get_bool overhead "$iface" overhead 0
	
	download="${download:-${halfduplex:+$upload}}"
	enum_classes "$classgroup"
	for dir in up${halfduplex} ${download:+down}; do
		case "$dir" in
			up)
				[ "$overhead" = 1 ] && upload=$(($upload * 98 / 100 - (15 * 128 / $upload)))
				dev="$device"
				rate="$upload"
				dl_mode=""
				prefix="cls"
			;;
			down)
				add_insmod imq numdevs="$num_imq"
				config_get imqdev "$iface" imqdev
				[ "$overhead" = 1 ] && download=$(($download * 98 / 100 - (80 * 1024 / $download)))
				dev="imq$imqdev"
				rate="$download"
				dl_mode=1
				prefix="d_cls"
			;;
			*) continue;;
		esac
		cstr=
		for class in $classes; do
			cls_var pktsize "$class" packetsize $dir 1500
			cls_var pktdelay "$class" packetdelay $dir 0
			cls_var maxrate "$class" limitrate $dir 100
			cls_var prio "$class" priority $dir 1
			cls_var avgrate "$class" avgrate $dir 0
			config_get classnr "$class" classnr
			append cstr "$classnr:$prio:$avgrate:$pktsize:$pktdelay:$maxrate" "$N"
		done
		append ${prefix}q "$(tcrules)" "$N"
		export dev_${dir}="ifconfig $dev up txqueuelen 5 >&- 2>&-
tc qdisc del dev $dev root >&- 2>&-
tc qdisc add dev $dev root handle 1: hfsc default ${class_default}0
tc class add dev $dev parent 1: classid 1:1 hfsc sc rate ${rate}kbit ul rate ${rate}kbit"
	done
	add_insmod cls_fw
	add_insmod sch_hfsc
	add_insmod sch_sfq
	add_insmod sch_red

	cat <<EOF
${INSMOD:+$INSMOD$N}${dev_up:+$dev_up
$clsq
}${imqdev:+$dev_down
$d_clsq
$d_clsl
$d_clsf
}
EOF
	unset INSMOD clsq clsf clsl d_clsq d_clsl d_clsf dev_up dev_down
}

start_interfaces() {
	local C="$1"
	for iface in $INTERFACES; do
		start_interface "$iface" "$C"
	done
}

add_rules() {
	local var="$1"
	local rules="$2"
	local prefix="$3"
	
	for rule in $rules; do
		unset iptrule
		config_get target "$rule" target
		config_get target "$target" classnr
		config_get options "$rule" options

		## If we want to override the TOS field, let's clear the DSCP field first.
		[ ! -z "$(echo $options | grep 'TOS')" ] && {
			s_options=${options%%TOS}
			add_insmod ipt_DSCP
			parse_matching_rule iptrule "$rule" "$s_options" "$prefix" "-j DSCP --set-dscp 0"
			append "$var" "$iptrule" "$N"
			unset iptrule
		}

		parse_matching_rule iptrule "$rule" "$options" "$prefix" "-j MARK --set-mark $target"
		append "$var" "$iptrule" "$N"
	done
}

start_cg() {
	local cg="$1"
	local iptrules
	local pktrules
	local sizerules
	local download
	enum_classes "$cg"
	add_rules iptrules "$ctrules" "iptables -t mangle -A ${cg}_ct"
	config_get classes "$cg" classes
	for class in $classes; do
		config_get mark "$class" classnr
		config_get maxsize "$class" maxsize
		[ -z "$maxsize" -o -z "$mark" ] || {
			add_insmod ipt_length
			append pktrules "iptables -t mangle -A ${cg} -m mark --mark $mark -m length --length $maxsize: -j MARK --set-mark 0" "$N"
		}
	done
	add_rules pktrules "$rules" "iptables -t mangle -A ${cg}"
	for iface in $INTERFACES; do
		config_get classgroup "$iface" classgroup
		config_get device "$iface" device
		config_get imqdev "$iface" imqdev
		config_get dl "$iface" download
		config_get halfduplex "$iface" halfduplex
		add_insmod ipt_IMQ
		append up "iptables -t mangle -A OUTPUT -o $device -j ${cg}" "$N"
		append up "iptables -t mangle -A FORWARD -o $device -j ${cg}" "$N"
		[ -z "$dl" ] || {
			append down "iptables -t mangle -A POSTROUTING -o $device -j ${cg}" "$N"
			[ -z "$halfduplex" ] || {
				append down "iptables -t mangle -A POSTROUTING -o $device -j IMQ --todev $imqdev" "$N"
			}
			append down "iptables -t mangle -A PREROUTING -i $device -j ${cg}" "$N"
			append down "iptables -t mangle -A PREROUTING -i $device -j IMQ --todev $imqdev" "$N"
		}
	done
	cat <<EOF
$INSMOD
iptables -t mangle -N ${cg} >&- 2>&-
iptables -t mangle -N ${cg}_ct >&- 2>&-
${iptrules:+${iptrules}${N}iptables -t mangle -A ${cg}_ct -j CONNMARK --save-mark}
iptables -t mangle -A ${cg} -j CONNMARK --restore-mark
iptables -t mangle -A ${cg} -m mark --mark 0 -j ${cg}_ct
$pktrules
$up$N${down:+${down}$N}
EOF
	unset INSMOD
}

start_firewall() {
	add_insmod ipt_multiport
	add_insmod ipt_CONNMARK
	cat <<EOF
iptables -t mangle -F
iptables -t mangle -X
EOF
	for group in $CG; do
		start_cg $group
	done
}

C="0"
INTERFACES=""
[ -e ./qos.conf ] && {
	. ./qos.conf
	config_cb
} || config_load qos

C="0"
for iface in $INTERFACES; do
	export C="$(($C + 1))"
done

case "$1" in
	all)
		start_interfaces "$C"
		start_firewall
	;;
	interface)
		start_interface "$2" "$C"
	;;
	interfaces)
		start_interfaces
	;;
	firewall)
		start_firewall
	;;
esac
