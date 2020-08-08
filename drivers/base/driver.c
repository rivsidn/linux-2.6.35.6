/*
 * driver.c - centralized device driver management
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "base.h"

static struct device *next_device(struct klist_iter *i)
{
	struct klist_node *n = klist_next(i);
	struct device *dev = NULL;
	struct device_private *dev_prv;

	if (n) {
		dev_prv = to_device_private_driver(n);
		dev = dev_prv->device;
	}
	return dev;
}

/**
 * driver_for_each_device - Iterator for devices bound to a driver.
 * @drv: Driver we're iterating.
 * @start: Device to begin with
 * @data: Data to pass to the callback.
 * @fn: Function to call for each device.
 *
 * Iterate over the @drv's list of devices calling @fn for each one.
 */
int driver_for_each_device(struct device_driver *drv, struct device *start,
			   void *data, int (*fn)(struct device *, void *))
{
	struct klist_iter i;
	struct device *dev;
	int error = 0;

	if (!drv)
		return -EINVAL;

	klist_iter_init_node(&drv->p->klist_devices, &i,
			     start ? &start->p->knode_driver : NULL);
	while ((dev = next_device(&i)) && !error)
		error = fn(dev, data);
	klist_iter_exit(&i);
	return error;
}
EXPORT_SYMBOL_GPL(driver_for_each_device);

/**
 * driver_find_device - device iterator for locating a particular device.
 * @drv: The device's driver
 * @start: Device to begin with
 * @data: Data to pass to match function
 * @match: Callback function to check device
 *
 * This is similar to the driver_for_each_device() function above, but
 * it returns a reference to a device that is 'found' for later use, as
 * determined by the @match callback.
 *
 * The callback should return 0 if the device doesn't match and non-zero
 * if it does.  If the callback returns non-zero, this function will
 * return to the caller and not iterate over any more devices.
 */
struct device *driver_find_device(struct device_driver *drv,
				  struct device *start, void *data,
				  int (*match)(struct device *dev, void *data))
{
	struct klist_iter i;
	struct device *dev;

	if (!drv)
		return NULL;

	klist_iter_init_node(&drv->p->klist_devices, &i,
			     (start ? &start->p->knode_driver : NULL));
	while ((dev = next_device(&i)))
		if (match(dev, data) && get_device(dev))
			break;
	klist_iter_exit(&i);
	return dev;
}
EXPORT_SYMBOL_GPL(driver_find_device);

/**
 * driver_create_file - create sysfs file for driver.
 * @drv: driver.
 * @attr: driver attribute descriptor(驱动属性描述符).
 */
int driver_create_file(struct device_driver *drv,
		       const struct driver_attribute *attr)
{
	int error;
	if (drv)
		error = sysfs_create_file(&drv->p->kobj, &attr->attr);
	else
		error = -EINVAL;
	return error;
}
EXPORT_SYMBOL_GPL(driver_create_file);

/**
 * driver_remove_file - remove sysfs file for driver.
 * @drv: driver.
 * @attr: driver attribute descriptor.
 */
void driver_remove_file(struct device_driver *drv,
			const struct driver_attribute *attr)
{
	if (drv)
		sysfs_remove_file(&drv->p->kobj, &attr->attr);
}
EXPORT_SYMBOL_GPL(driver_remove_file);

/**
 * driver_add_kobj - add a kobject below the specified driver
 * @drv: requesting device driver
 * @kobj: kobject to add below this driver
 * @fmt: format string that names the kobject
 *
 * You really don't want to do this, this is only here due to one looney
 * iseries driver, go poke those developers if you are annoyed about
 * this...
 */
int driver_add_kobj(struct device_driver *drv, struct kobject *kobj,
		    const char *fmt, ...)
{
	va_list args;
	char *name;
	int ret;

	va_start(args, fmt);
	name = kvasprintf(GFP_KERNEL, fmt, args);
	va_end(args);

	if (!name)
		return -ENOMEM;

	ret = kobject_add(kobj, &drv->p->kobj, "%s", name);
	kfree(name);
	return ret;
}
EXPORT_SYMBOL_GPL(driver_add_kobj);

/**
 * get_driver - increment driver reference count.
 * @drv: driver.
 */
struct device_driver *get_driver(struct device_driver *drv)
{
	if (drv) {
		struct driver_private *priv;
		struct kobject *kobj;

		kobj = kobject_get(&drv->p->kobj);
		priv = to_driver(kobj);
		return priv->driver;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(get_driver);

/**
 * put_driver - decrement driver's refcount.
 * @drv: driver.
 */
void put_driver(struct device_driver *drv)
{
	kobject_put(&drv->p->kobj);
}
EXPORT_SYMBOL_GPL(put_driver);

static int driver_add_groups(struct device_driver *drv,
			     const struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (groups) {
		for (i = 0; groups[i]; i++) {
			error = sysfs_create_group(&drv->p->kobj, groups[i]);
			if (error) {
				while (--i >= 0)
					sysfs_remove_group(&drv->p->kobj,
							   groups[i]);
				break;
			}
		}
	}
	return error;
}

static void driver_remove_groups(struct device_driver *drv,
				 const struct attribute_group **groups)
{
	int i;

	if (groups)
		for (i = 0; groups[i]; i++)
			sysfs_remove_group(&drv->p->kobj, groups[i]);
}

/**
 * driver_register - register driver with bus
 * @drv: driver to register
 *
 * We pass off most of the work to the bus_add_driver() call,
 * since most of the things we have to do deal with the bus
 * structures.
 * (我们将大部分工作放到bus_add_driver()函数中，因为大部分工作
 * 需要处理bus结构体)
 */
int driver_register(struct device_driver *drv)
{
	int ret;
	struct device_driver *other;

	BUG_ON(!drv->bus->p);

	//意思是应该用bus提供的方法，之前通过drv方法是老版本的，需要更新
	if ((drv->bus->probe && drv->probe) ||
	    (drv->bus->remove && drv->remove) ||
	    (drv->bus->shutdown && drv->shutdown))
		printk(KERN_WARNING "Driver '%s' needs updating - please use "
			"bus_type methods\n", drv->name);

	//同一个驱动名字只能注册一次
	other = driver_find(drv->name, drv->bus);
	if (other) {
		put_driver(other);	//由于增加了引用计数，此处需要减小
		printk(KERN_ERR "Error: Driver '%s' is already registered, "
			"aborting...\n", drv->name);
		return -EBUSY;
	}

	//将驱动添加到bus上
	ret = bus_add_driver(drv);
	if (ret)
		return ret;
	//同样是创建sysfs文件系统，从r8169驱动这边找过来，没找到何
	//时添加的，应该是其他驱动会用到。
	ret = driver_add_groups(drv, drv->groups);
	if (ret)
		bus_remove_driver(drv);
	return ret;
}
EXPORT_SYMBOL_GPL(driver_register);

/**
 * driver_unregister - remove driver from system.
 * @drv: driver.
 *
 * Again, we pass off most of the work to the bus-level call.
 */
void driver_unregister(struct device_driver *drv)
{
	if (!drv || !drv->p) {
		WARN(1, "Unexpected driver unregister!\n");
		return;
	}
	driver_remove_groups(drv, drv->groups);
	bus_remove_driver(drv);
}
EXPORT_SYMBOL_GPL(driver_unregister);

/**
 * driver_find - locate driver on a bus by its name.
 * 		(通过名字定位到在总线上的驱动)
 * @name: name of the driver.
 * @bus: bus to scan for the driver.
 *
 * Call kset_find_obj() to iterate over list of drivers on
 * a bus to find driver by name. Return driver if found.
 *
 * Note that kset_find_obj increments driver's reference count.
 * (注意kset_find_obj会增加驱动的引用计数)
 */
struct device_driver *driver_find(const char *name, struct bus_type *bus)
{
	struct kobject *k = kset_find_obj(bus->p->drivers_kset, name);
	struct driver_private *priv;

	if (k) {
		priv = to_driver(k);
		return priv->driver;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(driver_find);
