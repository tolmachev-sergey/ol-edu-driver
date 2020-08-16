#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#define EDU_LOG_PREFIX "edu: "
#define log(level, format, ...) printk(level EDU_LOG_PREFIX format "\n", __VA_ARGS__)
#define log_info(format, ...)   log(KERN_INFO, format, __VA_ARGS__)
#define log_error(format, ...)  log(KERN_ERR, format, __VA_ARGS__)

#define EDU_IDENT       0x00
 #define EDU_IDENT_MASK_MAJOR  (0xff000000)
 #define EDU_IDENT_MASK_MINOR  (0x00ff0000)
 #define EDU_IDENT_SHIFT_MAJOR (24)
 #define EDU_IDENT_SHIFT_MINOR (16)
 #define EDU_IDENT_MASK_MAGIC  (0x000000ff)
 #define EDU_IDENT_MAGIC       (0xed)

#define EDU_XOR         0x04
#define EDU_FACTORIAL   0x08
#define EDU_STATUS      0x20
 #define EDU_STATUS_COPMUTING  0x01
 #define EDU_STATUS_RAISE_INTR 0x80

#define EDU_INTR_STATUS 0x24
#define EDU_INTR_RAISE  0x60
#define EDU_INTR_ACK    0x64

#define EDU_DMA_SRC     0x80
#define EDU_DMA_DEST    0x88
#define EDU_DMA_CNT     0x90
#define EDU_DMA_CMD     0x98
 #define EDU_DMA_CMD_START      0x01
 #define EDU_DMA_CMD_DIR_TO_EDU 0x00
 #define EDU_DMA_CMD_DIR_TO_RAM 0x02
 #define EDU_DMA_CMD_RAISE_INTR 0x04

struct edu_driver {
    void __iomem *map;
};

static int edu_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int rc;
    u32 ident;
    u8 major, minor, magic;
    struct edu_driver *drv;

    drv = kzalloc(sizeof(struct edu_driver), GFP_KERNEL);
    if (!drv)
        return -ENOMEM;

    pci_set_drvdata(dev, drv);

    rc = pci_enable_device(dev);
    if (rc)
        goto free_drv;

    drv->map = pci_iomap(dev, 0, 0);
    if (!drv->map) {
        rc = -ENODEV;
        goto disable_device;
    }

    ident = ioread32(drv->map + EDU_IDENT);
    major = (ident & EDU_IDENT_MASK_MAJOR) >> EDU_IDENT_SHIFT_MAJOR;
    minor = (ident & EDU_IDENT_MASK_MINOR) >> EDU_IDENT_SHIFT_MINOR;
    magic = (ident & EDU_IDENT_MASK_MAGIC);

    if (magic != EDU_IDENT_MAGIC) {
        log_error("%s: magic (0x%x) is wrong, aborting", pci_name(dev), magic);
        rc = -ENODEV;
        goto disable_device;
    }

    if (major != 1) {
        log_error("%s: only version 1 is supported (%d), aborting",
                  pci_name(dev), major);
        rc = -ENODEV;
        goto disable_device;
    }

    log_info("%s: major %d minor %d", pci_name(dev), major, minor);

    return 0;

disable_device:
    pci_disable_device(dev);
free_drv:
    kfree(drv);

    return rc;
}

static void edu_remove(struct pci_dev *dev)
{
    struct edu_driver *drv = pci_get_drvdata(dev);

    pci_iounmap(dev, drv);
    pci_disable_device(dev);
    kfree(drv);
}

static struct pci_device_id edu_id_table[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { 0 }
};

static struct pci_driver edu_driver = {
    .name = "edu",
    .id_table = edu_id_table,
    .probe = edu_probe,
    .remove = edu_remove,
};
module_pci_driver(edu_driver);

MODULE_AUTHOR("Ilya Repko <Ilya.Repko@oktetlabs.ru>");
MODULE_DESCRIPTION("QEMU's 'edu' device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
