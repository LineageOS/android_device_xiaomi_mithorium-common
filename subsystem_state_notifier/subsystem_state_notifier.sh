#!/vendor/bin/sh

for SUBSYS in modem; do
	i=0
	unset SUBSYS_STATE_PATH
	until [ $i -gt 9 ] ; do
		SUBSYS_STATE_PATH="/sys/class/subsys/subsys_${SUBSYS}/device/subsys${i}/state"
		if [ -f "$SUBSYS_STATE_PATH" ]; then
			setprop "ro.vendor.subsys_state_notifier.${SUBSYS}.state_path" "$SUBSYS_STATE_PATH"
			break
		fi
		let i+=1
	done
done

exit 0
