/*
 * Copyright 2013-16 Board of Trustees of Stanford University
 * Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * pci.c - support for Linux user-level PCI access
 *
 * This file is loosely based on DPDK's PCI support:
 * BSD LICENSE
 * Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
 */

#include <ix/stddef.h>
#include <ix/pci.h>
#include <ix/log.h>
#include <ix/errno.h>
#include <ix/lock.h>

#include <pcidma.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/pci.h>
#include <sys/ioctl.h>

#include <dune.h>
#include <errno.h>


#define PCI_SYSFS_PATH "/sys/bus/pci/devices"

static int pcidma_fd;

static DEFINE_SPINLOCK(pcidma_open_lock);

static int sysfs_parse_val(const char *path, uint64_t *val)
{
	FILE *f;
	char buf[BUFSIZ];
	char *end = NULL;
	int ret = 0;

	f = fopen(path, "r");
	if (!f)
		return -EIO;

	if (!fgets(buf, sizeof(buf), f)) {
		ret = -EIO;
		goto out;
	}

	if (buf[0] == '\0') {
		ret = -EIO;
		goto out;
	}

	*val = strtoull(buf, &end, 0);
	if ((end == NULL) || (*end != '\n'))
		ret = -EIO;

out:
	fclose(f);
	return ret;
}

static int sysfs_store_val(const char *path, uint64_t val)
{
	FILE *f;
	int ret = 0;

	f = fopen(path, "w");
	if (!f)
		return -EIO;

	if (fprintf(f, "%ld", val) <= 0)
		ret = -EIO;

	fclose(f);
	return ret;
}

static int pci_scan_dev_info(struct pci_dev *dev, const char *dir_path)
{
	char file_path[PATH_MAX];
	uint64_t tmp;

	snprintf(file_path, sizeof(file_path), "%s/vendor", dir_path);
	if (sysfs_parse_val(file_path, &tmp))
		return -EIO;
	dev->vendor_id = (uint16_t)tmp;

	snprintf(file_path, sizeof(file_path), "%s/device", dir_path);
	if (sysfs_parse_val(file_path, &tmp))
		return -EIO;
	dev->device_id = (uint16_t)tmp;

	snprintf(file_path, sizeof(file_path), "%s/subsystem_vendor", dir_path);
	if (sysfs_parse_val(file_path, &tmp))
		return -EIO;
	dev->subsystem_vendor_id = (uint16_t)tmp;

	snprintf(file_path, sizeof(file_path), "%s/subsystem_device", dir_path);
	if (sysfs_parse_val(file_path, &tmp))
		return -EIO;
	dev->subsystem_device_id = (uint16_t)tmp;

	snprintf(file_path, sizeof(file_path), "%s/numa_node", dir_path);
	if (access(file_path, R_OK)) {
		dev->numa_node = -1;
	} else {
		if (sysfs_parse_val(file_path, &tmp))
			return -EIO;
		dev->numa_node = tmp;
	}

	snprintf(file_path, sizeof(file_path), "%s/max_vfs", dir_path);
	if (access(file_path, R_OK)) {
		dev->max_vfs = 0;
	} else {
		if (sysfs_parse_val(file_path, &tmp))
			return -EIO;
		dev->max_vfs = (uint16_t)tmp;
	}

	return 0;
}

static int pci_scan_dev_resources(struct pci_dev *dev, const char *dir_path)
{
	FILE *f;
	char file_path[PATH_MAX];
	char buf[BUFSIZ];
	int i, ret = 0;
	uint64_t start, end, flags;

	snprintf(file_path, sizeof(file_path), "%s/resource", dir_path);
	f = fopen(file_path, "r");
	if (f == NULL)
		return -EIO;

	for (i = 0; i < PCI_MAX_BARS; i++) {
		if (!fgets(buf, sizeof(buf), f)) {
			ret = -EIO;
			goto out;
		}

		if (sscanf(buf, "%lx %lx %lx",
			   &start, &end, &flags) != 3) {
			ret = -EINVAL;
			goto out;
		}

		dev->bars[i].start = start;
		dev->bars[i].len = end - start + 1;
		dev->bars[i].flags = flags;
	}

out:
	fclose(f);
	return ret;
}

/**
 * pci_str_to_addr - converts is string to a PCI address
 * @str: the input string
 * @addr: a pointer to the output address
 *
 * String format is DDDD:BB:SS.f, where D = domain (hex), B = bus (hex),
 * S = slot (hex), and f = function number (decimal).
 *
 * Returns 0 if successful, otherwise failure.
 */
int pci_str_to_addr(const char *str, struct pci_addr *addr)
{
	int ret;

	ret = sscanf(str, "%04hx:%02hhx:%02hhx.%hhd",
		     &addr->domain, &addr->bus,
		     &addr->slot, &addr->func);

	if (ret != 4)
		return -EINVAL;
	return 0;
}

static void pci_dump_dev(struct pci_dev *dev)
{
	int i;

	log_info("pci: created device %04x:%02x:%02x.%d, NUMA node %d\n",
		 dev->addr.domain, dev->addr.bus,
		 dev->addr.slot, dev->addr.func,
		 dev->numa_node);

	for (i = 0; i < PCI_MAX_BARS; i++) {
		struct pci_bar *bar = &dev->bars[i];
		if (!(bar->flags & PCI_BAR_MEM))
			continue;
		if (bar->flags & PCI_BAR_READONLY)
			continue;
		if (!bar->len)
			continue;

		log_info("pci:\tIOMEM - base %lx, len %lx\n",
			 bar->start, bar->len);
	}
}

/**
 * pci_alloc_dev - creates a PCI device
 * @addr: the address to scan
 *
 * This function allocates a PCI device and fully populates it with
 * information from sysfs.
 *
 * Returns a PCI dev, or NULL if failure.
 */
struct pci_dev *
pci_alloc_dev(const struct pci_addr *addr)
{
	char dir_path[PATH_MAX];
	struct pci_dev *dev;
	int ret;

	dev = malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));
	memcpy(&dev->addr, addr, sizeof(*addr));

	snprintf(dir_path, PATH_MAX, "%s/%04x:%02x:%02x.%d", PCI_SYSFS_PATH,
		 addr->domain, addr->bus, addr->slot, addr->func);

	if ((ret = pci_scan_dev_info(dev, dir_path)))
		goto fail;

	if ((ret = pci_scan_dev_resources(dev, dir_path)))
		goto fail;

	pci_dump_dev(dev);
	return dev;

fail:
	free(dev);
	return NULL;
}

/**
 * pci_find_mem_bar - locates a memory-mapped I/O bar
 * @dev: the PCI device
 * @count: specifies how many preceding memory bars to skip
 *
 * Returns a PCI bar, or NULL if failure.
 */
struct pci_bar *
pci_find_mem_bar(struct pci_dev *dev, int count)
{
	int i;
	struct pci_bar *bar;

	for (i = 0; i < PCI_MAX_BARS; i++) {
		bar = &dev->bars[i];
		if (!(bar->flags & PCI_BAR_MEM))
			continue;

		if (!count)
			return bar;
		count--;
	}

	return NULL;
}

static int pci_bar_to_idx(struct pci_dev *dev, struct pci_bar *bar)
{
	int idx = (bar - &dev->bars[0]) / sizeof(struct pci_bar);

	if (idx < 0 || idx >= PCI_MAX_BARS)
		return -EINVAL;

	return idx;
}

/**
 * pci_map_mem_bar - maps a memory-mapped I/O bar
 * @dev: the PCI device
 * @bar: the PCI bar
 * @wc: if true, use write-combining memory
 *
 * In most cases @wc should be false, but it is useful for framebuffers
 * and other cases where write order doesn't matter.
 *
 * Returns a virtual address, or NULL if fail.
 */
void *pci_map_mem_bar(struct pci_dev *dev, struct pci_bar *bar, bool wc)
{
	void *vaddr;
	int fd, idx;
	char path[PATH_MAX];
	struct pci_addr *addr = &dev->addr;

	if (bar->flags & PCI_BAR_READONLY)
		return NULL;
	if (bar->len == 0)
		return NULL;

	idx = pci_bar_to_idx(dev, bar);
	if (idx < 0)
		return NULL;

	if (wc) {
		if (!(bar->flags & PCI_BAR_PREFETCH))
			return NULL;
		snprintf(path, PATH_MAX, "%s/%04x:%02x:%02x.%d/resource%d_wc",
			 PCI_SYSFS_PATH, addr->domain, addr->bus,
			 addr->slot, addr->func, idx);
	} else {
		snprintf(path, PATH_MAX, "%s/%04x:%02x:%02x.%d/resource%d",
			 PCI_SYSFS_PATH, addr->domain, addr->bus,
			 addr->slot, addr->func, idx);
	}

	fd = open(path, O_RDWR);
	if (fd == -1)
		return NULL;

	vaddr = mmap(NULL, bar->len, PROT_READ | PROT_WRITE,
		     MAP_SHARED, fd, 0);
	close(fd);
	if (vaddr == MAP_FAILED)
		return NULL;

	/* FIXME: write-combining support needed */
	if (dune_vm_map_phys(pgroot, vaddr, bar->len,
			     (void *) dune_va_to_pa(vaddr),
			     PERM_R | PERM_W | PERM_UC)) {
		munmap(vaddr, bar->len);
		return NULL;
	}

	return vaddr;
}

/**
 * pci_unmap_mem_bar - unmaps a memory-mapped I/O bar
 * @bar: the bar to unmap
 * @vaddr: the address of the mapping
 */
void pci_unmap_mem_bar(struct pci_bar *bar, void *vaddr)
{
	dune_vm_unmap(pgroot, vaddr, bar->len);
	munmap(vaddr, bar->len);
}

int pci_enable_device(struct pci_dev *dev)
{
	char path[PATH_MAX];
	struct pci_addr *addr;
	uint64_t enable;

	addr = &dev->addr;

	snprintf(path, PATH_MAX, "%s/%04x:%02x:%02x.%d/enable", PCI_SYSFS_PATH, addr->domain, addr->bus, addr->slot, addr->func);
	if (sysfs_parse_val(path, &enable))
		return -EIO;

	if (enable)
		return 0;

	if (sysfs_store_val(path, 1))
		return -EIO;

	return 0;
}

int pci_set_master(struct pci_dev *dev)
{
	struct args_enable args;

	spin_lock(&pcidma_open_lock);
	if (!pcidma_fd)
		pcidma_fd = open("/dev/pcidma", O_RDONLY);
	spin_unlock(&pcidma_open_lock);

	if (pcidma_fd == -1)
		return -EIO;

	args.pci_loc.domain = dev->addr.domain;
	args.pci_loc.bus = dev->addr.bus;
	args.pci_loc.slot = dev->addr.slot;
	args.pci_loc.func = dev->addr.func;

	int ret = ioctl(pcidma_fd, PCIDMA_ENABLE, &args);
	if(ret)
		log_err("ioctl failed and returned errno %s \n",strerror(errno));
	return ret;
}
