#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>

#include <edu.h>

#define EDU_LEGACY_INTERRUPT 0

#define EDU_LOG_PREFIX "edu: "
#define log(level, format, ...) printk(level EDU_LOG_PREFIX format "\n", ##__VA_ARGS__)
#define log_info(format, ...)   log(KERN_INFO, format, ##__VA_ARGS__)
#define log_error(format, ...)  log(KERN_ERR, format, ##__VA_ARGS__)

#define EDU_IDENT       0x00
 #define EDU_IDENT_MASK_MAJOR  0xff000000
 #define EDU_IDENT_MASK_MINOR  0x00ff0000
 #define EDU_IDENT_SHIFT_MAJOR 24
 #define EDU_IDENT_SHIFT_MINOR 16
 #define EDU_IDENT_MASK_MAGIC  0x000000ff
 #define EDU_IDENT_MAGIC       0xed

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

struct edu_device {
    struct pci_dev *pdev;
    void __iomem *map;

    struct cdev cdev;
    struct mutex lock;
    unsigned open_count;
    struct device *dev;
};

static struct class *edu_class;
static atomic_t n_devices = ATOMIC_INIT(0);  /* total amount of devices created */
static bool factorial_calculating_flag = false;

static long edu_do_xor(struct edu_device *edu, unsigned long arg)
{
    struct edu_xor_cmd __user *cmd = (void __user *)(arg);
    u32 val_in, val_out;

    if (get_user(val_in, &cmd->val_in))
        return -EINVAL;

    iowrite32(val_in, edu->map + EDU_XOR);
    val_out = ioread32(edu->map + EDU_XOR);

    if (put_user(val_out, &cmd->val_out))
        return -EINVAL;

    return 0;
}


static long edu_do_factorial(struct edu_device *edu, unsigned long arg)
{
    struct edu_factorial_cmd __user *cmd = (void __user *)(arg);
    u32 val_in;
    u32 EDU_STATUS_val_in;

    if (get_user(val_in, &cmd->val_in))
        return -EINVAL;

    if (factorial_calculating_flag)
        return -EBUSY;

    EDU_STATUS_val_in = ioread32(edu->map + EDU_STATUS);
    EDU_STATUS_val_in |= 0x80;
    iowrite32(EDU_STATUS_val_in, edu->map + EDU_STATUS);

    factorial_calculating_flag = true;
    iowrite32(val_in, edu->map + EDU_FACTORIAL);

    return 0;
}

static long edu_do_intr(struct edu_device *edu, unsigned long arg)
{
    struct edu_intr_cmd __user *cmd = (void __user *)(arg);
    u32 val_in;

    if (get_user(val_in, &cmd->val_in))
        return -EINVAL;
    
    iowrite32(val_in, edu->map + EDU_INTR_RAISE);

    return 0;
}

static long edu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct edu_device *edu = file->private_data;

    switch (cmd)
    {
        case EDU_IOCTL_XOR:
            return edu_do_xor(edu, arg);

        case EDU_IOCTL_FACTORIAL:
            return edu_do_factorial(edu, arg);

        case EDU_IOCTL_INTR:
            return edu_do_intr(edu, arg);

        default:
            return -EINVAL;
    }
}

static int edu_open(struct inode *inode, struct file *file)
{
    struct edu_device *edu = container_of(inode->i_cdev, struct edu_device,
                                          cdev);

    mutex_lock(&edu->lock);

    if (edu->open_count) {
        mutex_unlock(&edu->lock);
        return -EBUSY;
    }

    edu->open_count++;
    file->private_data = edu;

    mutex_unlock(&edu->lock);

    return 0;
}

static int edu_release(struct inode *inode, struct file *file)
{
    struct edu_device *edu = file->private_data;

    mutex_lock(&edu->lock);
    edu->open_count--;
    mutex_unlock(&edu->lock);

    return 0;
}

static struct file_operations edu_fops = {
    .owner = THIS_MODULE,
    .open = edu_open,
    .release = edu_release,
    .unlocked_ioctl = edu_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

static int edu_create_chardev(struct edu_device *edu)
{
    int rc;
    unsigned n;
    dev_t devt;
    struct device *dev;

    n = atomic_fetch_add(1, &n_devices);
    devt = MKDEV(EDU_MAJOR, n);
    edu->open_count = 0;
    mutex_init(&edu->lock);

    cdev_init(&edu->cdev, &edu_fops);
    edu->cdev.owner = THIS_MODULE;

    rc = cdev_add(&edu->cdev, devt, 1);
    if (rc) {
        log_error("%s: failed to add a cdev (rc = %d)", pci_name(edu->pdev), rc);
        return rc;
    }

    dev = device_create(edu_class, &edu->pdev->dev, devt, NULL, "edu%u", n);
    if (IS_ERR(dev)) {
        rc = PTR_ERR(dev);
        log_error("%s: failed to create a device (rc = %d)", pci_name(edu->pdev),
                  rc);
        goto err_cdev_del;
    }

    edu->dev = dev;

    return 0;

err_cdev_del:
    cdev_del(&edu->cdev);

    return rc;
}

static void edu_destroy_chardev(struct edu_device *edu)
{
    device_destroy(edu_class, edu->dev->devt);
    cdev_del(&edu->cdev);
}

static irqreturn_t edu_irq(int irq, void *data)
{
    struct pci_dev *dev = data;
    struct edu_device *edu = pci_get_drvdata(data);
    u32 status;

    status = ioread32(edu->map + EDU_INTR_STATUS);
    if (status) {
        u32 edu_st = ioread32(edu->map + EDU_STATUS);
        if (factorial_calculating_flag && (edu_st & 0x01) == 0) {
            u32 val_out = ioread32(edu->map + EDU_FACTORIAL);
            log_info("Factorial = (%d)", val_out);
            iowrite32(status, edu->map + EDU_INTR_ACK);
            factorial_calculating_flag = false;
        } else {
            log_info("%s: got interrupted (%x)", pci_name(dev), status);
            iowrite32(status, edu->map + EDU_INTR_ACK);
        }
    }
    else {
        return IRQ_NONE;
    }

    iowrite32(status, edu->map + EDU_INTR_ACK);
    return IRQ_HANDLED;
}

static int edu_check_version(struct pci_dev *dev, void __iomem *map)
{
    u32 ident;
    u8 major, minor, magic;

    ident = ioread32(map + EDU_IDENT);
    major = (ident & EDU_IDENT_MASK_MAJOR) >> EDU_IDENT_SHIFT_MAJOR;
    minor = (ident & EDU_IDENT_MASK_MINOR) >> EDU_IDENT_SHIFT_MINOR;
    magic = (ident & EDU_IDENT_MASK_MAGIC);

    if (magic != EDU_IDENT_MAGIC) {
        log_error("%s: magic (0x%x) is wrong, aborting", pci_name(dev), magic);
        return -ENODEV;
    }

    if (major != 1) {
        log_error("%s: only major version 1 is supported (%d), aborting",
                  pci_name(dev), major);
        return -ENODEV;
    }

    log_info("%s: major %d minor %d", pci_name(dev), major, minor);

    return 0;
}

static int edu_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int rc;
    struct edu_device *edu;

    edu = devm_kzalloc(&dev->dev, sizeof(struct edu_device), GFP_KERNEL);
    if (!edu)
        return -ENODEV;

    edu->pdev = dev;
    pci_set_drvdata(dev, edu);

    rc = pci_enable_device(dev);
    if (rc)
        goto err_free_edu;

    pci_set_master(dev);

    rc = pci_request_region(dev, 0, "edu-bar0");
    if (rc) {
        log_error("%s: failed to request BAR0 (rc = %d)", pci_name(dev), rc);
        goto err_disable_device;
    }

    edu->map = pci_iomap(dev, 0, 0);
    if (!edu->map) {
        log_error("%s: failed to map BAR0", pci_name(dev));
        rc = -ENODEV;
        goto err_free_region;
    }

    rc = edu_check_version(dev, edu->map);
    if (rc)
        goto err_free_region;

#if EDU_LEGACY_INTERRUPT
    rc = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_LEGACY);
#else
    rc = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSI);
#endif
    if (rc < 0) {
        log_error("%s: failed to allocate IRQ (rc = %d)", pci_name(dev), rc);
        goto err_free_region;
    }

#if EDU_LEGACY_INTERRUPT
    rc = request_irq(pci_irq_vector(dev, 0), edu_irq, IRQF_SHARED, "edu-irq",
                     dev);
#else
    rc = request_irq(pci_irq_vector(dev, 0), edu_irq, 0, "edu-irq", dev);
#endif
    if (rc) {
        log_error("%s: failed to request IRQ (rc = %d)", pci_name(dev), rc);
        goto err_free_vectors;
    }

    rc = edu_create_chardev(edu);
    if (rc)
        goto err_free_irq;

    return 0;

err_free_irq:
    free_irq(pci_irq_vector(dev, 0), dev);
err_free_vectors:
    pci_free_irq_vectors(dev);
err_free_region:
    pci_release_region(dev, 0);
err_disable_device:
    pci_disable_device(dev);
err_free_edu:
    kfree(edu);

    return rc;
}

static void edu_remove(struct pci_dev *dev)
{
    struct edu_device *edu = pci_get_drvdata(dev);

    edu_destroy_chardev(edu);
    free_irq(pci_irq_vector(dev, 0), dev);
    pci_free_irq_vectors(dev);
    pci_iounmap(dev, edu->map);
    pci_release_region(dev, 0);
    pci_disable_device(dev);
    devm_kfree(&dev->dev, edu);
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

static int __init edu_init(void)
{
    int rc;

    edu_class = class_create(THIS_MODULE, "edu");
    if (IS_ERR(edu_class)) {
        log_error("failed to create 'edu' class");
        return PTR_ERR(edu_class);
    }

    rc = pci_register_driver(&edu_driver);
    if (rc) {
        log_error("failed to register PCI device (rc = %d)", rc);
        goto err_class_destroy;
    }

    return 0;

err_class_destroy:
    class_destroy(edu_class);

    return rc;
}

static void __exit edu_exit(void)
{
    pci_unregister_driver(&edu_driver);
    class_destroy(edu_class);
}

module_init(edu_init);
module_exit(edu_exit);

MODULE_AUTHOR("Ilya Repko <Ilya.Repko@oktetlabs.ru>");
MODULE_DESCRIPTION("QEMU's 'edu' device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
