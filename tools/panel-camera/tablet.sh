#!/bin/sh
# Run a command on the tablet over ssh.
#   tablet.sh 'devmem 0x20008150 32'
# The address moves: the Wi-Fi MAC is randomised per boot. Override with
# TAQ102_HOST. The key is the dedicated ~/.ssh/taq102, installed on /root
# of the ramdisk, so it must be reinstalled after a reboot of the rescue image.
exec ssh -i "$HOME/.ssh/taq102" \
	-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	-o IdentitiesOnly=yes -o ConnectTimeout=8 \
	"root@${TAQ102_HOST:-192.168.1.57}" "$@"
