# Log

## 23 May

Here is what has been achieved so far as of 23 May.

There are 3 main things of interest:

1. CXL Switch QEMU Device
2. CXL Switch Device Driver module
3. CXL Switch user-space program

### CXL Switch QEMU Device

Uses 3 host-backed memory files for replication.
Replicates stores to the regions.
Route load from one region.

### CXL Switch Device Driver Module

Registers the QEMU device which is `/dev/cxl-switch`

### User-space program

Uses `mmap` to map the memory region exposed by the CXL Switch.
Writes to it.
Manually read the memory files with hexdump.

### What's happening

A. Initialization & Device Setup Phase:

    QEMU Startup & Device Definition:
        Start QEMU, providing arguments to instantiate your cxl-switch device. Example: -device cxl-switch,id=mycxl,mem-size=256M,memdev0=ram0,memdev1=ram1,memdev2=ram2.
        You also define the host memory backends: -object memory-backend-file,id=ram0,size=256M,mem-path=/tmp/ram0.img,share=on (and similarly for ram1, ram2). These .img files will physically store the replicated data on the host.

    QEMU Type Registration (type_init(pci_cxl_switch_register_types)):
        The cxl_switch_info (a TypeInfo struct) is registered with QEMU's type system, defining the cxl-switch device type.

    QEMU Instance Init (cxl_switch_instance_init):
        When QEMU creates an instance of cxl-switch, this function runs. It sets a default s->mem_size (e.g., 128MiB, but this will be overridden by the mem-size=256M property) and initializes s->backing_mem_id pointers to NULL.

    QEMU Device Realization (pci_cxl_switch_realize):
        This is a key PCI device lifecycle function in QEMU.
        It reads the mem-size (256MB from your command line) and memdev0/1/2 string properties (e.g., "ram0", "ram1", "ram2") into the CXLSwitchState *s.
        Initializes s->lock (a QemuMutex for protecting concurrent access to shared device state like health_status and during read/write operations).

    Link to Host Backing Stores:
        For each memdevX property (e.g., "ram0"):
            object_resolve_path("ram0", ...) finds the HostMemoryBackend object you defined with -object memory-backend-file,id=ram0,....
            This HostMemoryBackend (s->backing_hmb[i]) provides the actual host memory.
            s->backing_mr[i] = host_memory_backend_get_memory(...) gets the QEMU MemoryRegion associated with this host RAM (e.g., pointing to the mmaped /tmp/ram0.img).
            host_memory_backend_set_mapped(s->backing_hmb[i], true) marks the backend as in use.

    Initialize Guest-Visible Replicated Memory Region (s->replicated_mr):
        memory_region_init_io(&s->replicated_mr, ..., &cxl_switch_mem_ops, s, "cxl-switch-replicated-mem", s->mem_size): This creates the primary memory region (256MB in size) that the guest OS will see.
        It's initialized with cxl_switch_mem_ops, meaning any read/write to this region by the guest will call your cxl_switch_mem_read or cxl_switch_mem_write functions. s (the CXLSwitchState pointer) is passed as opaque data to these callbacks.

    Register BAR0 (pci_register_bar):
        pci_register_bar(pdev, 0, ..., &s->replicated_mr): This tells QEMU to expose s->replicated_mr as BAR0 of this PCI device to the guest. The flags indicate it's a 64-bit, prefetchable memory region.

    QEMU Presents PCI Device to Guest:
        The QEMU emulated CXL Switch, with Vendor ID 0x1AF4 and Device ID 0x1337, and its BAR0 (which is s->replicated_mr) are now visible on the guest's emulated PCI bus.

    Guest Kernel: PCI Driver Registration (Your Linux Driver):
        Your cxl_switch_init_module is called (e.g., via insmod).
        pci_register_driver(&cxl_switch_driver) tells the guest Linux kernel about your driver and the PCI IDs (cxl_switch_ids) it supports.

    Guest Kernel: Device Discovery & Probe (cxl_switch_pci_probe):
        The guest kernel's PCI subsystem matches the emulated CXL switch (VID:0x1AF4, DID:0x1337) with your driver.
        It calls your cxl_switch_pci_probe function.

    Driver: Enable Device (pci_enable_device): Standard PCI device enabling.

    Driver: Get BAR0 Info:
        pci_resource_start(pdev, 0) returns the guest physical base address of BAR0 (which QEMU set to be s->replicated_mr).
        pci_resource_len(pdev, 0) returns its size (256MB).

    Driver: Request BAR0 Region (pci_request_region): Claims the BAR0 MMIO resource in the guest.

    Driver: Map BAR0 to Kernel VA (pcim_iomap): dev->bar0_kva gets a kernel virtual address for BAR0. Not directly used by user mmap path but good practice.

    Driver: Create Character Device (alloc_chrdev_region, etc.): Sets up the character device, associating it with cxl_switch_fops (which contains your cxl_switch_open, cxl_switch_mmap implementations).

    Driver: Create Device Node (device_create): Creates /dev/cxl_switch0 in the guest's filesystem using the char device.

B. Runtime Data Flow (User-Space Write via mmap):

    User Program: open("/dev/cxl_switch0", O_RDWR | O_SYNC):
        Your user-space C program opens the device file created by the driver. O_SYNC attempts to make writes synchronous, though for MMIO, caching behavior is key.
    Guest Kernel: cxl_switch_open:
        The VFS routes the open call to your driver's cxl_switch_open.
    Driver: Store Context: filp->private_data is set to point to the struct cxl_switch_dev, making device-specific data available for subsequent calls like mmap.
    User Program: mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0):
        MAP_SIZE is 256MB. PROT_READ | PROT_WRITE allows reading and writing. MAP_SHARED ensures writes are visible to the "device" (and other mappers). fd is the file descriptor from open. 0 is the offset within BAR0.
    Guest Kernel: cxl_switch_mmap:
        The kernel routes the mmap call to your driver's cxl_switch_mmap.
    Driver: Calculate Physical Address & Size:
        phys = pci_resource_start(pdev, 0) (since user offset is 0) gets the guest physical base address of BAR0.
        vsize is set to MAP_SIZE (256MB).
    Driver: Set Page Protections:
        vma->vm_page_prot = pgprot_noncached(...): Crucial for MMIO. Disables CPU caching for this memory region.
        vm_flags_set(vma, VM_IO | ...): Marks the region as I/O memory.
    Driver: io_remap_pfn_range:
        This guest kernel function maps the guest physical address range of BAR0 directly into the user-space program's virtual address space.
    Mapping Complete: mmap returns a pointer (mapped_mem) to the user-space program. This pointer is a user virtual address.
    User Program: mapped_mem[0] = 0xAABBCCDD;:
        The user program writes a 32-bit value (0xAABBCCDD) to the start of the mapped region. volatile (used in your user code) ensures the compiler doesn't optimize away the access.
    Guest CPU/MMU Translation:
        The guest CPU attempts to write to the user virtual address. The guest MMU translates this to the guest physical address corresponding to the start of BAR0 (which is the start of QEMU's s->replicated_mr).
    QEMU Hypervisor Intercepts MMIO Write:
        QEMU detects a write to the guest physical address that falls within the MMIO range of s->replicated_mr.
    QEMU Invokes cxl_switch_mem_write:
        Because s->replicated_mr was initialized with cxl_switch_mem_ops, QEMU calls s->replicated_mr.ops->write, which is your cxl_switch_mem_write function.
        opaque (which is s), addr (offset within BAR0, here 0), val (0xAABBCCDD), and size (4) are passed.
    QEMU Device: Acquire Lock (qemu_mutex_lock(&s->lock)):
        The mutex is locked to ensure exclusive access while performing the replicated write and checking health status.
    QEMU Device: Replicate Write Loop:
        The code iterates i = 0 to 2 (for the three backing stores):
            Checks s->health_status[i].
            ram_ptr = memory_region_get_ram_ptr(s->backing_mr[i]): Gets a direct host pointer to the memory of the i-th backing store (e.g., the mmaped content of /tmp/ram0.img).
            stl_le_p(ram_ptr + addr, (uint32_t)val): Writes the 32-bit value (little-endian) to the calculated host address (ram_ptr + offset). This is the actual data replication happening on the host.
            successful_writes is incremented.
    QEMU Device: Release Lock (qemu_mutex_unlock(&s->lock)): The mutex is unlocked.
    Return Path (Conceptual for a Write, Actual for a Read):
        For a write, the cxl_switch_mem_write function completes.
        If this were a read (mapped_mem[0] on the RHS of an assignment):
            cxl_switch_mem_read would be called.
            It would lock, find one healthy s->backing_mr[i], read data from it using ldl_le_p, unlock, and return the uint64_t data.
            This data would propagate back through QEMU, the guest kernel, to the user-space program's mapped_mem[0].