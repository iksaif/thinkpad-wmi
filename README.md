# thinkpad-wmi

Linux Driver for Thinkpad WMI interface, allows you to control most
BIOS settings from Linux, and maybe more.

## sysfs interface

Directory: /sys/devices/platform/thinkpad-wmi/

Each setting exposed by the WMI interface is available under its own name
in this sysfs directory. Read from the file to get the list of options
and write an option to the file to set it.

Additionally, there are some extra files.

### Password

Must contain the BIOS password, if set, to be able to do any change.

### PasswordEncoding

Unclear, can be '', 'ascii' or 'scancode'.

### PasswordKbdLang

Unclear, can be '', 'us', 'fr' or 'gr'.

### password_settings

Display password related settings.

### load_default_settings

Reset all settings to factory default.

## debugfs interface

The debugfs interface maps closely to the WMI Interface (see driver and doc).

* bios_settings: show all BIOS settings
* bios_setting: show BIOS setting for <instance>
* list_valid_choices: list settings for <argument>
* set_bios_settings: call set bios settings command with <argument>.
* save_bios_settings call save bios settings command with <argument>.
* discard_bios_settings: call discard bios settings command with <argument>.
* load_default: call load default with <argument>.
* set_bios_password: call set BIOS password with <argument>.
* argument: argument to be used in various commands.
* instance: setting instance.
* instance_count: number of settings.
* password_settings: password settings.

## References

Thinkpad WMI interface documentation:
http://download.lenovo.com/ibmdl/pub/pc/pccbbs/thinkcentre_pdf/hrdeploy_en.pdf