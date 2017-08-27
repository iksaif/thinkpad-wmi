/*
 * Thinkpad WMI hotkey driver
 *
 * Copyright(C) 2012 Corentin Chary <corentin.chary@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/acpi.h>

#define	THINKPAD_WMI_FILE	"thinkpad-wmi"

MODULE_AUTHOR("Corentin Chary <corentin.chary@gmail.com>");
MODULE_DESCRIPTION("Thinkpad WMI Driver");
MODULE_LICENSE("GPL");

/* WMI inteface */

/**
 * Name:
 *  Lenovo_BiosSetting
 * Description:
 *  Get item name and settings for current WMI instance.
 * Type:
 *  Query
 * Returns:
 *  "Item,Value"
 * Example:
 *  "WakeOnLAN,Enable"
 */
#define LENOVO_BIOS_SETTING_GUID		\
	"51F5230E-9677-46CD-A1CF-C0B23EE34DB7"

/**
 * Name:
 *  Lenovo_SetBiosSetting
 * Description:
 *  Change the BIOS setting to the desired value using the Lenovo_SetBiosSetting
 *  class. To save the settings, use the Lenovo_SaveBiosSetting class.
 *  BIOS settings and values are case sensitive.
 *  After making changes to the BIOS settings, you must reboot the computer
 *  before the changes will take effect.
 * Type:
 *  Method
 * Arguments:
 *  "Item,Value,Password,Encoding,KbdLang;"
 * Example:
 *  "WakeOnLAN,Disable,pswd,ascii,us;"
 */
#define LENOVO_SET_BIOS_SETTINGS_GUID		\
	"98479A64-33F5-4E33-A707-8E251EBBC3A1"

/**
 * Name:
 *  Lenovo_SaveBiosSettings
 * Description:
 *  Save any pending changes in settings.
 * Type:
 *  Method
 * Arguments:
 *  "Password,Encoding,KbdLang;"
 * Example:
 * "pswd,ascii,us;"
 */
#define LENOVO_SAVE_BIOS_SETTINGS_GUID		\
	"6A4B54EF-A5ED-4D33-9455-B0D9B48DF4B3"


/**
 * Name:
 *  Lenovo_DiscardBiosSettings
 * Description:
 *  Discard any pending changes in settings.
 * Type:
 *  Method
 * Arguments:
 *  "Password,Encoding,KbdLang;"
 * Example:
 *  "pswd,ascii,us;"
 */
#define LENOVO_DISCARD_BIOS_SETTINGS_GUID	\
	"74F1EBB6-927A-4C7D-95DF-698E21E80EB5"

/**
 * Name:
 *  Lenovo_LoadDefaultSettings
 * Description:
 *  Load default BIOS settings. Use Lenovo_SaveBiosSettings to save the
 *  settings.
 * Type:
 *  Method
 * Arguments:
 *  "Password,Encoding,KbdLang;"
 * Example:
 *  "pswd,ascii,us;"
 */
#define LENOVO_LOAD_DEFAULT_SETTINGS_GUID	\
	"7EEF04FF-4328-447C-B5BB-D449925D538D"

/**
 * Name:
 *  Lenovo_BiosPasswordSettings
 * Description:
 *  Return BIOS Password settings
 * Type:
 *  Query
 * Returns:
 *  PasswordMode, PasswordState, MinLength, MaxLength,
 *  SupportedEncoding, SupportedKeyboard
 */
#define LENOVO_BIOS_PASSWORD_SETTINGS_GUID		\
	"8ADB159E-1E32-455C-BC93-308A7ED98246"

/**
 * Name:
 *  Lenovo_SetBiosPassword
 * Description:
 *  Change a specific password.
 *  - BIOS settings cannot be changed at the same boot as power-on
 *    passwords (POP) and hard disk passwords (HDP). If you want to change
 *    BIOS settings and POP or HDP, you must reboot the system after changing
 *    one of them.
 *  - A password cannot be set using this method when one does not already
 *    exist. Passwords can only be updated or cleared.
 * Type:
 *  Method
 * Arguments:
 *  "PasswordType,CurrentPassword,NewPassword,Encoding,KbdLang;"
 * Example:
 *  "pop,oldpop,newpop,ascii,us;”
 */
#define LENOVO_SET_BIOS_PASSWORD_GUID	\
	"2651D9FD-911C-4B69-B94E-D0DED5963BD7"

/**
 * Name:
 *  Lenovo_GetBiosSelections
 * Description:
 *  Return a list valid settings for a given item.
 * Type:
 *  Method
 * Arguments:
 *  "Item"
 * Returns:
 *  "Value1,Value2,Value3,..."
 * Example:
 *  -> "FlashOverLAN"
 *  <- "Enabled,Disabled"
 */
#define LENOVO_GET_BIOS_SELECTIONS_GUID	\
	"7364651A-132F-4FE7-ADAA-40C6C7EE2E3B"

/**
 * Name:
 *  ???
 * Type:
 *  Method
 * Arguments:
 *  ???
 * Example:
 *  ???
 * WMI-Internals:
 *  Return big chunk of data
 */
#define LENOVO_QUERY_GUID			\
	"05901221-D566-11D1-B2F0-00A0C9062910"

/* Return values */

enum {
	/*
	 * "Success"
	 * Operation completed successfully.
	 */
	THINKPAD_WMI_SUCCESS = 0,
	/*
	 * "Not Supported"
	 * The feature is not supported on this system.
	 */
	THINKPAD_WMI_NOT_SUPPORTED = -ENODEV,
	/*
	 * "Invalid"
	 * The item or value provided is not valid parameter
	 */
	THINKPAD_WMI_INVALID = -EINVAL,
	/*
	 * "Access Denied"
	 * The change could not be made due to an authentication problem.
	 * If a supervisor password exists, the correct supervisor password
	 * must be provided.
	 */
	THINKPAD_WMI_ACCESS_DENIED = -EPERM,
	/* "System Busy"
	 * BIOS changes have already been made that need to be committed.
	 * Reboot the system and try again.
	 */
	THINKPAD_WMI_SYSTEM_BUSY = -EBUSY
};

/* Only add an alias on this one, since it's the one used
 * in thinkpad_wmi_probe */
MODULE_ALIAS("wmi:"LENOVO_BIOS_SETTING_GUID);

struct thinkpad_wmi_pcfg {
	uint32_t password_mode;
	uint32_t password_state;
	uint32_t min_length;
	uint32_t max_length;
	uint32_t supported_encodings;
	uint32_t supported_keyboard;
};

/*
 * thinkpad_wmi/       - debugfs root directory
 *   bios_settings
 *   bios_setting
 *   list_valid_choices
 *   set_bios_settings
 *   save_bios_settings
 *   discard_bios_settings
 *   load_default
 *   set_bios_password
 *   argument
 *   instance
 *   instance_count
 *   bios_password_settings
 */
struct thinkpad_wmi_debug {
	struct dentry *root;

	u8 instances_count;
	u8 instance;
	char argument[512];
};

struct thinkpad_wmi {
	struct platform_device *platform_device;

	int settings_count;

	char password[64];
	char password_encoding[64];
	char password_kbdlang[4]; /* 2 bytes for \n\0 */
	char auth_string[256];
	char password_type[64];

	bool can_set_bios_settings;
	bool can_discard_bios_settings;
	bool can_load_default_settings;
	bool can_get_bios_selections;
	bool can_set_bios_password;
	bool can_get_password_settings;

	char *settings[256];
	struct dev_ext_attribute *devattrs;
	struct thinkpad_wmi_debug debug;
};

/* helpers */
static int thinkpad_wmi_errstr_to_err(const char *errstr)
{
	if (!strcmp(errstr, "Success"))
		return THINKPAD_WMI_SUCCESS;
	if (!strcmp(errstr, "Not Supported"))
		return THINKPAD_WMI_NOT_SUPPORTED;
	if (!strcmp(errstr, "Invalid"))
		return THINKPAD_WMI_INVALID;
	if (!strcmp(errstr, "Access Denied"))
		return THINKPAD_WMI_ACCESS_DENIED;
	if (!strcmp(errstr, "System Busy"))
		return THINKPAD_WMI_SYSTEM_BUSY;

	pr_debug("Unknown error string: '%s'", errstr);

	return -EINVAL;
}

static int thinkpad_wmi_extract_error(const struct acpi_buffer *output)
{
	const union acpi_object *obj;
	int ret;

	obj = output->pointer;
	if (!obj || obj->type != ACPI_TYPE_STRING || !obj->string.pointer)
		return -EIO;

	ret = thinkpad_wmi_errstr_to_err(obj->string.pointer);
	kfree(obj);
	return ret;
}

static int thinkpad_wmi_simple_call(const char *guid,
				    const char *arg)
{
	const struct acpi_buffer input = { strlen(arg), (char *)arg };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	status = wmi_evaluate_method(guid, 0, 0, &input, &output);

	if (ACPI_FAILURE(status))
		return -EIO;

	return thinkpad_wmi_extract_error(&output);
}

static int thinkpad_wmi_extract_output_string(const struct acpi_buffer *output,
					      char **string)
{
	const union acpi_object *obj;

	obj = output->pointer;
	if (!obj || obj->type != ACPI_TYPE_STRING || !obj->string.pointer)
		return -EIO;

	*string = kstrdup(obj->string.pointer, GFP_KERNEL);
	kfree(obj);
	return *string ? 0 : -ENOMEM;
}

static int thinkpad_wmi_bios_setting(int item, char **value)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	status = wmi_query_block(LENOVO_BIOS_SETTING_GUID, item, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	return thinkpad_wmi_extract_output_string(&output, value);
}

static int thinkpad_wmi_get_bios_selections(const char *item, char **value)
{
	const struct acpi_buffer input = { strlen(item), (char *)item };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	status = wmi_evaluate_method(LENOVO_GET_BIOS_SELECTIONS_GUID,
				     0, 0, &input, &output);

	if (ACPI_FAILURE(status))
		return -EIO;

	return thinkpad_wmi_extract_output_string(&output, value);
}

static int thinkpad_wmi_set_bios_settings(const char *settings)
{
	return thinkpad_wmi_simple_call(LENOVO_SET_BIOS_SETTINGS_GUID,
					settings);
}

static int thinkpad_wmi_save_bios_settings(const char *password)
{
	return thinkpad_wmi_simple_call(LENOVO_SAVE_BIOS_SETTINGS_GUID,
					password);
}

static int thinkpad_wmi_discard_bios_settings(const char *password)
{
	return thinkpad_wmi_simple_call(LENOVO_DISCARD_BIOS_SETTINGS_GUID,
					password);
}

static int thinkpad_wmi_load_default(const char *password)
{
	return thinkpad_wmi_simple_call(LENOVO_LOAD_DEFAULT_SETTINGS_GUID,
					password);
}

static int thinkpad_wmi_set_bios_password(const char *settings)
{
	return thinkpad_wmi_simple_call(LENOVO_SET_BIOS_PASSWORD_GUID,
					settings);
}

static int thinkpad_wmi_password_settings(struct thinkpad_wmi_pcfg *pcfg)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	const union acpi_object *obj;
	acpi_status status;

	status = wmi_query_block(LENOVO_BIOS_PASSWORD_SETTINGS_GUID, 0,
				 &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = output.pointer;
	if (!obj || obj->type != ACPI_TYPE_BUFFER || !obj->buffer.pointer)
		return -EIO;
	if (obj->buffer.length != sizeof(*pcfg)) {
		pr_warn("Unknown pcfg buffer length %d\n", obj->buffer.length);
		kfree(obj);
		return -EIO;
	}

	memcpy(pcfg, obj->buffer.pointer, obj->buffer.length);
	kfree(obj);
	return 0;
}

/* sysfs */

#define to_ext_attr(x) container_of(x, struct dev_ext_attribute, attr)

static ssize_t show_setting(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	int item = (uintptr_t)ea->var;
	char *name = thinkpad->settings[item];
	char *settings = NULL, *choices = NULL, *value;
	ssize_t count = 0;
	int ret;

	ret = thinkpad_wmi_bios_setting(item, &settings);
	if (ret)
		return ret;
	if (!settings)
		return -EIO;

	if (thinkpad->can_get_bios_selections) {
		ret = thinkpad_wmi_get_bios_selections(name, &choices);
		if (ret)
			goto error;
		if (!choices || !*choices) {
			ret = -EIO;
			goto error;
		}
	}

	value = strchr(settings, ',');
	if (!value)
		goto error;
	value++;

	count = sprintf(buf, "%s\n", value);
	if (choices)
		count += sprintf(buf + count, "%s\n", choices);

error:
	kfree(settings);
	kfree(choices);
	return ret ? ret : count;
}

static ssize_t store_setting(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	int item_idx = (uintptr_t)ea->var;
	const char *item = thinkpad->settings[item_idx];
	int ret;
	size_t buffer_size;
	char *buffer;

	/* Format: 'Item,Value,Authstring;' */
	buffer_size = (strlen(item) + 1 + count + 1 +
		       sizeof(thinkpad->auth_string) + 2);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	strcpy(buffer, item);
	strcat(buffer, ",");
	strncat(buffer, buf, count);
	if (count)
		strim(buffer);
	if (*thinkpad->auth_string) {
		strcat(buffer, ",");
		strcat(buffer, thinkpad->auth_string);
	}
	strcat(buffer, ";");

	ret = thinkpad_wmi_set_bios_settings(buffer);
	if (ret)
		goto end;

	ret = thinkpad_wmi_save_bios_settings(thinkpad->auth_string);
	if (ret) {
		/* Try to discard the settings if we failed to apply them. */
		thinkpad_wmi_discard_bios_settings(thinkpad->auth_string);
		goto end;
	}
	ret = count;

end:
	kfree(buffer);
	return ret;
}


/* Password related sysfs methods */
static ssize_t show_auth(struct thinkpad_wmi *thinkpad, char *buf,
			 const char *data, size_t size)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return sprintf(buf, "%s\n", data ? : "(nil)");
}

/* Create the auth string from password chunks */
static void update_auth_string(struct thinkpad_wmi *thinkpad)
{
	if (!*thinkpad->password) {
		/* No password at all */
		thinkpad->auth_string[0] = '\0';
		return;
	}
	strcpy(thinkpad->auth_string, thinkpad->password);

	if (*thinkpad->password_encoding) {
		strcat(thinkpad->auth_string, ",");
		strcat(thinkpad->auth_string, thinkpad->password_encoding);
	}

	if (*thinkpad->password_kbdlang) {
		strcat(thinkpad->auth_string, ",");
		strcat(thinkpad->auth_string, thinkpad->password_kbdlang);
	}
}

static ssize_t store_auth(struct thinkpad_wmi *thinkpad,
			  const char *buf, size_t count,
			  char *dst, size_t size)
{
	ssize_t ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (count > size - 1)
		return -EINVAL;

	/* dst may be being reused, NUL-terminate */
	ret = strscpy(dst, buf, size);
	if (ret < 0)
		return ret;
	if (count)
		strim(dst);

	update_auth_string(thinkpad);

	return count;
}

#define THINKPAD_WMI_CREATE_AUTH_ATTR(_name, _uname, _mode)		\
	static ssize_t show_##_name(struct device *dev,			\
				    struct device_attribute *attr,	\
				    char *buf)				\
	{								\
		struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);	\
									\
		return show_auth(thinkpad, buf,				\
				 thinkpad->_name,			\
				 sizeof(thinkpad->_name));		\
	}								\
	static ssize_t store_##_name(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t count)	\
	{								\
		struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);	\
									\
		return store_auth(thinkpad, buf, count,			\
				  thinkpad->_name,			\
				  sizeof(thinkpad->_name));		\
	}								\
	static struct device_attribute dev_attr_##_name = {		\
		.attr = {						\
			.name = _uname,					\
			.mode = _mode },				\
		.show   = show_##_name,					\
		.store  = store_##_name,				\
	}

THINKPAD_WMI_CREATE_AUTH_ATTR(password, "password", S_IRUSR|S_IWUSR);
THINKPAD_WMI_CREATE_AUTH_ATTR(password_encoding, "password_encoding",
			      S_IRUSR|S_IWUSR);
THINKPAD_WMI_CREATE_AUTH_ATTR(password_kbdlang, "password_kbd_lang",
			      S_IRUSR|S_IWUSR);
THINKPAD_WMI_CREATE_AUTH_ATTR(password_type, "password_type", S_IRUSR|S_IWUSR);

static ssize_t show_password_settings(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct thinkpad_wmi_pcfg pcfg;
	ssize_t ret;

	ret = thinkpad_wmi_password_settings(&pcfg);
	if (ret)
		return ret;
	ret += sprintf(buf, "password_mode:       %#x\n", pcfg.password_mode);
	ret += sprintf(buf + ret, "password_state:      %#x\n",
		       pcfg.password_state);
	ret += sprintf(buf + ret, "min_length:          %d\n", pcfg.min_length);
	ret += sprintf(buf + ret, "max_length:          %d\n", pcfg.max_length);
	ret += sprintf(buf + ret, "supported_encodings: %#x\n",
		       pcfg.supported_encodings);
	ret += sprintf(buf + ret, "supported_keyboard:  %#x\n",
		       pcfg.supported_keyboard);
	return ret;
}

static DEVICE_ATTR(password_settings, S_IRUSR, show_password_settings, NULL);

static ssize_t store_password_change(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);
	size_t buffer_size;
	char *buffer;
	ssize_t ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Format: 'PasswordType,CurrentPw,NewPw,Encoding,KbdLang;' */

	/* auth_string is the size of CurrentPassword,Encoding,KbdLang */
	buffer_size = (sizeof(thinkpad->password_type) + 1 + count + 1 +
		       sizeof(thinkpad->auth_string) + 2);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	strcpy(buffer, thinkpad->password_type);

	if (*thinkpad->password) {
		strcat(buffer, ",");
		strcat(buffer, thinkpad->password);
	}
	strcat(buffer, ",");
	strncat(buffer, buf, count);
	if (count)
		strim(buffer);

	if (*thinkpad->password_encoding) {
		strcat(buffer, ",");
		strcat(buffer, thinkpad->password_encoding);
	}
	if (*thinkpad->password_kbdlang) {
		strcat(buffer, ",");
		strcat(buffer, thinkpad->password_kbdlang);
	}
	strcat(buffer, ";");

	ret = thinkpad_wmi_set_bios_password(buffer);
	if (ret)
		return ret;

	return count;
}

static struct device_attribute dev_attr_password_change = {
	.attr = {
		.name = "password_change",
		.mode = S_IWUSR },
	.store  = store_password_change,
};


static ssize_t store_load_default(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct thinkpad_wmi *thinkpad = dev_get_drvdata(dev);

	return thinkpad_wmi_load_default(thinkpad->auth_string);
}

static DEVICE_ATTR(load_default_settings, S_IWUSR, NULL, store_load_default);

static struct attribute *platform_attributes[] = {
	&dev_attr_password_settings.attr,
	&dev_attr_password.attr,
	&dev_attr_password_encoding.attr,
	&dev_attr_password_kbdlang.attr,
	&dev_attr_password_type.attr,
	&dev_attr_password_change.attr,
	&dev_attr_load_default_settings.attr,
	NULL
};

static umode_t thinkpad_sysfs_is_visible(struct kobject *kobj,
					 struct attribute *attr,
					 int idx)
{
	bool supported = true;

	return supported ? attr->mode : 0;
}

static struct attribute_group platform_attribute_group = {
	.is_visible	= thinkpad_sysfs_is_visible,
	.attrs		= platform_attributes
};

static void thinkpad_wmi_sysfs_exit(struct platform_device *device)
{
	struct thinkpad_wmi *thinkpad = platform_get_drvdata(device);
	int i;

	sysfs_remove_group(&device->dev.kobj, &platform_attribute_group);

	if (!thinkpad->devattrs)
		return;

	for (i = 0; i < thinkpad->settings_count; ++i) {
		struct dev_ext_attribute *deveattr = &thinkpad->devattrs[i];
		struct device_attribute *devattr = &deveattr->attr;

		if (devattr->attr.name)
			device_remove_file(&device->dev, devattr);
	}
	kfree(thinkpad->devattrs);
	thinkpad->devattrs = NULL;
}

static int __init thinkpad_wmi_sysfs_init(struct platform_device *device)
{
	struct thinkpad_wmi *thinkpad = platform_get_drvdata(device);
	struct dev_ext_attribute *devattrs;
	int count = thinkpad->settings_count;
	int i, ret;

	devattrs = kmalloc(sizeof(*devattrs) * count, GFP_KERNEL);
	if (!devattrs)
		return -ENOMEM;
	thinkpad->devattrs = devattrs;

	for (i = 0; i < count; ++i) {
		struct dev_ext_attribute *deveattr = &devattrs[i];
		struct device_attribute *devattr = &deveattr->attr;

		sysfs_attr_init(&devattr->attr);
		devattr->attr.name = thinkpad->settings[i];
		devattr->attr.mode = S_IRUGO | S_IWUSR;
		devattr->show = show_setting;
		devattr->store = store_setting;
		deveattr->var = (void *)(uintptr_t)i;
		ret = device_create_file(&device->dev, devattr);
		if (ret) {
			/* Name is used to check is file has been created. */
			devattr->attr.name = NULL;
			return ret;
		}
	}

	return sysfs_create_group(&device->dev.kobj, &platform_attribute_group);
}

/*
 * Platform device
 */
static int __init thinkpad_wmi_platform_init(struct thinkpad_wmi *thinkpad)
{
	return thinkpad_wmi_sysfs_init(thinkpad->platform_device);
}

static void thinkpad_wmi_platform_exit(struct thinkpad_wmi *thinkpad)
{
	thinkpad_wmi_sysfs_exit(thinkpad->platform_device);
}

/* debugfs */

static ssize_t dbgfs_write_argument(struct file *file,
				    const char __user *userbuf,
				    size_t count, loff_t *pos)
{
	struct thinkpad_wmi *thinkpad = file->f_path.dentry->d_inode->i_private;
	char *kernbuf = thinkpad->debug.argument;
	size_t size = sizeof(thinkpad->debug.argument);

	if (count > PAGE_SIZE - 1)
		return -EINVAL;

	if (count > size - 1)
		return -EINVAL;

	if (copy_from_user(kernbuf, userbuf, count))
		return -EFAULT;

	kernbuf[count] = 0;

	strim(kernbuf);

	return count;
}

static int dbgfs_show_argument(struct seq_file *m, void *v)
{
	struct thinkpad_wmi *thinkpad = m->private;

	seq_printf(m, "%s\n", thinkpad->debug.argument);
	return 0;
}

static int thinkpad_wmi_debugfs_argument_open(struct inode *inode,
					      struct file *file)
{
	struct thinkpad_wmi *thinkpad = inode->i_private;

	return single_open(file, dbgfs_show_argument, thinkpad);
}

static const struct file_operations thinkpad_wmi_debugfs_argument_fops = {
	.owner		= THIS_MODULE,
	.open		= thinkpad_wmi_debugfs_argument_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dbgfs_write_argument,
};

struct thinkpad_wmi_debugfs_node {
	struct thinkpad_wmi *thinkpad;
	char *name;
	int (*show)(struct seq_file *m, void *data);
};

static void show_bios_setting_line(struct thinkpad_wmi *thinkpad,
				   struct seq_file *m, int i, bool list_valid)
{
	int ret;
	char *settings = NULL, *choices = NULL, *p;

	ret = thinkpad_wmi_bios_setting(i, &settings);
	if (ret || !settings)
		return;

	p = strchr(settings, ',');
	if (p)
		*p = '=';
	seq_printf(m, "%s", settings);


	if (!thinkpad->can_get_bios_selections)
		goto line_feed;

	if (p)
		*p = '\0';

	ret = thinkpad_wmi_get_bios_selections(settings, &choices);
	if (ret || !choices || !*choices)
		goto line_feed;

	seq_printf(m, "\t[%s]", choices);

line_feed:
	kfree(settings);
	kfree(choices);
	seq_puts(m, "\n");
}

static int dbgfs_bios_settings(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;
	int i;

	for (i = 0; i < thinkpad->settings_count; ++i)
		show_bios_setting_line(thinkpad, m, i, true);

	return 0;
}

static int dbgfs_bios_setting(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	show_bios_setting_line(m->private, m, thinkpad->debug.instance, false);
	return 0;
}

static int dbgfs_list_valid_choices(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;
	char *choices = NULL;
	int ret;

	ret = thinkpad_wmi_get_bios_selections(thinkpad->debug.argument,
					      &choices);

	if (ret || !choices || !*choices) {
		kfree(choices);
		return -EIO;
	}

	seq_printf(m, "%s\n", choices);
	kfree(choices);
	return 0;
}

static int dbgfs_set_bios_settings(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	return thinkpad_wmi_set_bios_settings(thinkpad->debug.argument);
}

static int dbgfs_save_bios_settings(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	return thinkpad_wmi_save_bios_settings(thinkpad->debug.argument);
}

static int dbgfs_discard_bios_settings(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	return thinkpad_wmi_discard_bios_settings(thinkpad->debug.argument);
}

static int dbgfs_load_default(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	return thinkpad_wmi_load_default(thinkpad->debug.argument);
}

static int dbgfs_set_bios_password(struct seq_file *m, void *data)
{
	struct thinkpad_wmi *thinkpad = m->private;

	return thinkpad_wmi_set_bios_password(thinkpad->debug.argument);
}

static int dbgfs_bios_password_settings(struct seq_file *m, void *data)
{
	struct thinkpad_wmi_pcfg pcfg;
	int ret;

	ret = thinkpad_wmi_password_settings(&pcfg);
	if (ret)
		return ret;
	seq_printf(m, "password_mode:       %#x\n", pcfg.password_mode);
	seq_printf(m, "password_state:      %#x\n", pcfg.password_state);
	seq_printf(m, "min_length:          %d\n", pcfg.min_length);
	seq_printf(m, "max_length:          %d\n", pcfg.max_length);
	seq_printf(m, "supported_encodings: %#x\n", pcfg.supported_encodings);
	seq_printf(m, "supported_keyboard:  %#x\n", pcfg.supported_keyboard);
	return 0;
}

static struct thinkpad_wmi_debugfs_node thinkpad_wmi_debug_files[] = {
	{ NULL, "bios_settings", dbgfs_bios_settings },
	{ NULL, "bios_setting", dbgfs_bios_setting },
	{ NULL, "list_valid_choices", dbgfs_list_valid_choices },
	{ NULL, "set_bios_settings", dbgfs_set_bios_settings },
	{ NULL, "save_bios_settings", dbgfs_save_bios_settings },
	{ NULL, "discard_bios_settings", dbgfs_discard_bios_settings },
	{ NULL, "load_default", dbgfs_load_default },
	{ NULL, "set_bios_password", dbgfs_set_bios_password },
	{ NULL, "bios_password_settings", dbgfs_bios_password_settings },
};

static int thinkpad_wmi_debugfs_open(struct inode *inode, struct file *file)
{
	struct thinkpad_wmi_debugfs_node *node = inode->i_private;

	return single_open(file, node->show, node->thinkpad);
}

static const struct file_operations thinkpad_wmi_debugfs_io_ops = {
	.owner = THIS_MODULE,
	.open  = thinkpad_wmi_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void __init thinkpad_wmi_debugfs_exit(struct thinkpad_wmi *thinkpad)
{
	debugfs_remove_recursive(thinkpad->debug.root);
}

static int thinkpad_wmi_debugfs_init(struct thinkpad_wmi *thinkpad)
{
	struct dentry *dent;
	int i;

	thinkpad->debug.instances_count = thinkpad->settings_count;

	thinkpad->debug.root = debugfs_create_dir(THINKPAD_WMI_FILE, NULL);
	if (!thinkpad->debug.root) {
		pr_err("failed to create debugfs directory");
		goto error_debugfs;
	}

	dent = debugfs_create_file("argument", S_IRUGO | S_IWUSR,
				   thinkpad->debug.root, thinkpad,
				   &thinkpad_wmi_debugfs_argument_fops);
	if (!dent)
		goto error_debugfs;

	dent = debugfs_create_u8("instance", S_IRUGO | S_IWUSR,
				 thinkpad->debug.root,
				 &thinkpad->debug.instance);
	if (!dent)
		goto error_debugfs;

	dent = debugfs_create_u8("instances_count", S_IRUGO,
				 thinkpad->debug.root,
				 &thinkpad->debug.instances_count);
	if (!dent)
		goto error_debugfs;

	for (i = 0; i < ARRAY_SIZE(thinkpad_wmi_debug_files); i++) {
		struct thinkpad_wmi_debugfs_node *node;

		node = &thinkpad_wmi_debug_files[i];

		/* Filter non-present interfaces */
		if (!strcmp(node->name, "set_bios_settings") &&
		    !thinkpad->can_set_bios_settings)
			continue;
		if (!strcmp(node->name, "dicard_bios_settings") &&
		    !thinkpad->can_discard_bios_settings)
			continue;
		if (!strcmp(node->name, "load_default_settings") &&
		    !thinkpad->can_load_default_settings)
			continue;
		if (!strcmp(node->name, "get_bios_selections") &&
		    !thinkpad->can_get_bios_selections)
			continue;
		if (!strcmp(node->name, "set_bios_password") &&
		    !thinkpad->can_set_bios_password)
			continue;
		if (!strcmp(node->name, "bios_password_settings") &&
		    !thinkpad->can_get_password_settings)
			continue;

		node->thinkpad = thinkpad;
		dent = debugfs_create_file(node->name, S_IFREG | S_IRUGO,
					   thinkpad->debug.root, node,
					   &thinkpad_wmi_debugfs_io_ops);
		if (!dent) {
			pr_err("failed to create debug file: %s\n", node->name);
			goto error_debugfs;
		}
	}


	return 0;

error_debugfs:
	thinkpad_wmi_debugfs_exit(thinkpad);
	return -ENOMEM;
}

/* Base driver */
static void __init thinkpad_wmi_analyze(struct thinkpad_wmi *thinkpad)
{
	acpi_status status;
	int i = 0;

	/* Try to find the number of valid settings of this machine
	 * and use it to create sysfs attributes */
	for (i = 0; i < 0xFF; ++i) {
		char *item = NULL;
		char *p;

		status = thinkpad_wmi_bios_setting(i, &item);
		if (ACPI_FAILURE(status))
			break;
		if (!item || !*item)
			break;
		/* Remove the value part */
		p = strchr(item, ',');
		if (p)
			*p = '\0';
		thinkpad->settings[i] = item; /* Cache setting name */
	}

	thinkpad->settings_count = i;
	pr_info("Found %d settings", thinkpad->settings_count);

	if (wmi_has_guid(LENOVO_SET_BIOS_SETTINGS_GUID) &&
	    wmi_has_guid(LENOVO_SAVE_BIOS_SETTINGS_GUID)) {
		thinkpad->can_set_bios_settings = true;
	}

	if (wmi_has_guid(LENOVO_DISCARD_BIOS_SETTINGS_GUID))
		thinkpad->can_discard_bios_settings = true;

	if (wmi_has_guid(LENOVO_LOAD_DEFAULT_SETTINGS_GUID))
		thinkpad->can_load_default_settings = true;

	if (wmi_has_guid(LENOVO_GET_BIOS_SELECTIONS_GUID))
		thinkpad->can_get_bios_selections = true;

	if (wmi_has_guid(LENOVO_SET_BIOS_PASSWORD_GUID))
		thinkpad->can_set_bios_password = true;

	if (wmi_has_guid(LENOVO_BIOS_PASSWORD_SETTINGS_GUID))
		thinkpad->can_get_password_settings = true;
}

static int __init thinkpad_wmi_add(struct platform_device *pdev)
{
	struct thinkpad_wmi *thinkpad;
	int err;

	thinkpad = kzalloc(sizeof(struct thinkpad_wmi), GFP_KERNEL);
	if (!thinkpad)
		return -ENOMEM;

	thinkpad->platform_device = pdev;
	platform_set_drvdata(thinkpad->platform_device, thinkpad);

	thinkpad_wmi_analyze(thinkpad);

	err = thinkpad_wmi_platform_init(thinkpad);
	if (err)
		goto error_platform;

	err = thinkpad_wmi_debugfs_init(thinkpad);
	if (err)
		goto error_debugfs;

	return 0;

error_debugfs:
	thinkpad_wmi_platform_exit(thinkpad);
error_platform:
	kfree(thinkpad);
	return err;
}

static int __exit thinkpad_wmi_remove(struct platform_device *device)
{
	struct thinkpad_wmi *thinkpad;
	int i;

	thinkpad = platform_get_drvdata(device);
	thinkpad_wmi_debugfs_exit(thinkpad);
	thinkpad_wmi_platform_exit(thinkpad);

	for (i = 0; thinkpad->settings[i]; ++i) {
		kfree(thinkpad->settings[i]);
		thinkpad->settings[i] = NULL;
	}

	kfree(thinkpad);
	return 0;
}

static struct platform_device *platform_device;

static int __init thinkpad_wmi_probe(struct platform_device *pdev)
{
	if (!wmi_has_guid(LENOVO_BIOS_SETTING_GUID)) {
		pr_warn("Lenovo_BiosSetting GUID missing\n");
		return -ENODEV;
	}

	return thinkpad_wmi_add(pdev);
}

static struct platform_driver platform_driver = {
	.remove = __exit_p(thinkpad_wmi_remove),
	.driver = {
		.name = THINKPAD_WMI_FILE,
		.owner = THIS_MODULE,
	},
};

static int __init thinkpad_wmi_init(void)
{
	platform_device = platform_create_bundle(&platform_driver,
						 thinkpad_wmi_probe,
						 NULL, 0, NULL, 0);
	if (IS_ERR(platform_device))
		return PTR_ERR(platform_device);
	return 0;
}

static void __exit thinkpad_wmi_exit(void)
{
	platform_device_unregister(platform_device);
	platform_driver_unregister(&platform_driver);
}

module_init(thinkpad_wmi_init);
module_exit(thinkpad_wmi_exit);
