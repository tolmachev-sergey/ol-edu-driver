#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

static int edu_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    return 0;
}

static void edu_remove(struct pci_dev *dev)
{

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
