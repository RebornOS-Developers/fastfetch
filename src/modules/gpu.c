#include "fastfetch.h"

#include <string.h>
#include <dlfcn.h>
#include <pci/pci.h>

#define FF_GPU_MODULE_NAME "GPU"
#define FF_GPU_NUM_FORMAT_ARGS 3

static void handleGPU(FFinstance* instance, struct pci_access* pacc, struct pci_dev* dev, FFcache* cache, uint8_t counter, char*(*ffpci_lookup_name)(struct pci_access*, char*, int, int, ...))
{
    char vendor[512];
    ffpci_lookup_name(pacc, vendor, sizeof(vendor), PCI_LOOKUP_VENDOR, dev->vendor_id, dev->device_id);

    const char* vendorPretty;
    if(strcasecmp(vendor, "Advanced Micro Devices, Inc. [AMD/ATI]") == 0)
        vendorPretty = "AMD ATI";
    else if(strcasecmp(vendor, "NVIDIA Corporation") == 0)
        vendorPretty = "Nvidia";
    else if(strcasecmp(vendor, "Intel Corporation") == 0)
        vendorPretty = "Intel";
    else
        vendorPretty = vendor;

    char name[512];
    ffpci_lookup_name(pacc, name, sizeof(name), PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);

    FFstrbuf gpu;
    ffStrbufInitA(&gpu, 128);

    ffStrbufSetF(&gpu, "%s %s", vendorPretty, name);

    ffPrintAndAppendToCache(instance, FF_GPU_MODULE_NAME, counter, &instance->config.gpuKey, cache, &gpu, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, (FFformatarg[]){
        {FF_FORMAT_ARG_TYPE_STRING, vendor},
        {FF_FORMAT_ARG_TYPE_STRING, vendorPretty},
        {FF_FORMAT_ARG_TYPE_STRING, name}
    });

    ffStrbufDestroy(&gpu);
}

void ffPrintGPU(FFinstance* instance)
{
    if(ffPrintFromCache(instance, FF_GPU_MODULE_NAME, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS))
        return;

    void* pci;
    if(instance->config.libPCI.length == 0)
        pci = dlopen("libpci.so", RTLD_LAZY);
    else
        pci = dlopen(instance->config.libPCI.chars, RTLD_LAZY);

    if(pci == NULL)
    {
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlopen(\"libpci.so\", RTLD_LAZY) == NULL");
        return;
    }

    struct pci_access*(*ffpci_alloc)() = dlsym(pci, "pci_alloc");
    if(ffpci_alloc == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_alloc\") == NULL");
        return;
    }

    void(*ffpci_init)(struct pci_access*) = dlsym(pci, "pci_init");
    if(ffpci_init == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_init\") == NULL");
        return;
    }

    void(*ffpci_scan_bus)(struct pci_access*) = dlsym(pci, "pci_scan_bus");
    if(ffpci_scan_bus == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_init\") == NULL");
        return;
    }

    int(*ffpci_fill_info)(struct pci_dev*, int) = dlsym(pci, "pci_fill_info");
    if(ffpci_fill_info == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_fill_info\") == NULL");
        return;
    }

    char*(*ffpci_lookup_name)(struct pci_access*, char*, int, int, ...) = dlsym(pci, "pci_lookup_name");
    if(ffpci_lookup_name == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_lookup_name\") == NULL");
        return;
    }

    void(*ffpci_cleanup)(struct pci_access*) = dlsym(pci, "pci_cleanup");
    if(ffpci_cleanup == NULL)
    {
        dlclose(pci);
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "dlsym(pci, \"pci_cleanup\") == NULL");
        return;
    }

    uint8_t counter = 1;

    FFcache cache;
    ffCacheOpenWrite(instance, FF_GPU_MODULE_NAME, &cache);

    struct pci_access *pacc;
    struct pci_dev *dev;

    pacc = ffpci_alloc();
    ffpci_init(pacc);
    ffpci_scan_bus(pacc);
    for (dev=pacc->devices; dev; dev=dev->next)
    {
        ffpci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
        char class[1024];
        ffpci_lookup_name(pacc, class, sizeof(class), PCI_LOOKUP_CLASS, dev->device_class);
        if(
            strcasecmp("VGA compatible controller", class) == 0 ||
            strcasecmp("3D controller", class)             == 0 ||
            strcasecmp("Display controller", class)        == 0 )
        {
            handleGPU(instance, pacc, dev, &cache, counter++, ffpci_lookup_name);
        }
    }
    ffpci_cleanup(pacc);

    ffCacheClose(&cache);

    dlclose(pci);

    if(counter == 1)
        ffPrintError(instance, FF_GPU_MODULE_NAME, 0, &instance->config.gpuKey, &instance->config.gpuFormat, FF_GPU_NUM_FORMAT_ARGS, "No GPU found");
}