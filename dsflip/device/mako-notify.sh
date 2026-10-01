#!/bin/sh
# ROCKNIXDS: stands in for ROCKNIX's /usr/bin/mako-notify (bind-mounted over it at boot by the autostart hook
# rocknixds-mako-notify, which first copies the original to mako-notify.real beside this file).
#
# ROCKNIX's volume-key service (input_sense) calls `mako-notify "Volume: N%" -no-es` from inside its evtest
# pipeline. With the nightly's mako-notify (a binary since 20260930) that showed nothing in the menus, for two
# reasons found on the RG DS Plus: the call inherits the never-ending key pipe as its standard input, which the
# binary waits on before posting (nothing showed, and the next key event was eaten); and when it can reach sway
# (SWAYSOCK is in the service's environment) it works out that EmulationStation is in front and, told -no-es,
# posts nothing at all. Without SWAYSOCK it posts to mako, which draws it over the menus. Standard input closed,
# SWAYSOCK dropped: the indicator shows again.
unset SWAYSOCK
exec /storage/.config/drastic/dsflip/mako-notify.real "$@" </dev/null
