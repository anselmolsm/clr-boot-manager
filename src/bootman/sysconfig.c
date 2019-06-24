/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016-2018 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <blkid/blkid.h>

#include "bootman.h"
#include "bootman_private.h"
#include "files.h"
#include "log.h"
#include "nica/files.h"
#include "system_stub.h"

void cbm_free_sysconfig(SystemConfig *config)
{
        if (!config) {
                return;
        }
        free(config->prefix);
        free(config->boot_device);
        cbm_probe_free(config->root_device);
        free(config);
}

int cbm_get_fstype(const char *boot_device)
{
        int rc;
        blkid_probe pr;
        const char *data;
        int ret = 0;

        pr = blkid_new_probe_from_filename(boot_device);
        if (!pr) {
                LOG_ERROR("%s: failed to create a new libblkid probe",
                                boot_device);
                exit(EXIT_FAILURE);
        }

        blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_TYPE);
        rc = blkid_do_safeprobe(pr);
        if (rc != 0) {
                LOG_ERROR("%s: blkid_do_safeprobe() failed", boot_device);
                exit(EXIT_FAILURE);
        }

        rc = blkid_probe_lookup_value(pr, "TYPE", &data, NULL);
        if (rc != 0) {
                LOG_ERROR("%s: blkid_probe_lookup_value() failed", boot_device);
                exit(EXIT_FAILURE);
        }

        if ((strcmp(data, "ext2") == 0) ||
            (strcmp(data, "ext3") == 0) ||
            (strcmp(data, "ext4") == 0))
                ret = BOOTLOADER_CAP_EXTFS;
        else if (strcmp(data, "vfat") == 0)
                ret = BOOTLOADER_CAP_FATFS;

        blkid_free_probe(pr);

        return ret;
}

SystemConfig *cbm_inspect_root(const char *path, bool image_mode)
{
        if (!path) {
                return NULL;
        }
        SystemConfig *c = NULL;
        char *realp = NULL;
        char *boot = NULL;
        char *rel = NULL;
        bool native_uefi = false;

        realp = realpath(path, NULL);
        if (!realp) {
                LOG_ERROR("Path specified does not exist: %s", path);
                return NULL;
        }

        c = calloc(1, sizeof(struct SystemConfig));
        if (!c) {
                DECLARE_OOM();
                free(realp);
                return NULL;
        }
        c->prefix = realp;
        c->wanted_boot_mask = 0;

        /* Determine if this is a native UEFI system. This means we're in a full
         * native mode and have /sys/firmware/efi available. This does not throw
         * the image generation, and subsequent updates to the legacy image
         * wouldn't have a UEFI vfs available.
         */
        if (!image_mode) {
                /* typically /sys, but we forcibly fail this with our tests */
                autofree(char) *fw_path =
                    string_printf("%s/firmware/efi", cbm_system_get_sysfs_path());
                native_uefi = nc_file_exists(fw_path);
        }

        /* Find legacy relative to root, on GPT, assuming we're not booted using
         * UEFI. This is due to GPT being able to contain a legacy boot device
         * *and* an ESP at the same time. Native UEFI takes precedence.
         */
        if (!native_uefi || image_mode) {
                boot = get_legacy_boot_device(realp);
        }

        if (boot) {
                c->boot_device = boot;
                c->wanted_boot_mask = BOOTLOADER_CAP_LEGACY | BOOTLOADER_CAP_GPT;
                LOG_INFO("Discovered legacy boot device: %s", boot);
                goto refine_device;
        }

        /* Now try to find the system ESP */
        if (!image_mode) {
                boot = get_boot_device();
        }
        if (boot) {
                c->boot_device = boot;
                c->wanted_boot_mask = BOOTLOADER_CAP_UEFI | BOOTLOADER_CAP_GPT;
                LOG_INFO("Discovered UEFI ESP: %s", boot);
                goto refine_device;
        }

        /* At this point, we have no boot device available, try to inspect
         * the root system if we're in image mode.
         */
        if (!image_mode) {
                c->wanted_boot_mask = native_uefi ? BOOTLOADER_CAP_UEFI : BOOTLOADER_CAP_LEGACY;
        } else {
                /* At this point, just assume we have a plain UEFI system */
                c->wanted_boot_mask = BOOTLOADER_CAP_UEFI;
        }

refine_device:
        /* Our probe methods are GPT only. If we found one, it's definitely GPT */
        if (c->boot_device) {
                rel = realpath(c->boot_device, NULL);
                if (!rel) {
                        LOG_FATAL("Cannot determine boot device: %s %s",
                                  c->boot_device,
                                  strerror(errno));
                } else {
                        free(c->boot_device);
                        c->boot_device = rel;
                        LOG_INFO("Fully resolved boot device: %s", rel);
                }
                c->wanted_boot_mask |= BOOTLOADER_CAP_GPT;
        }

        /* determine fstype of the boot_device */
        if (c->boot_device) {
                c->wanted_boot_mask |= cbm_get_fstype(c->boot_device);
        }

        c->root_device = cbm_probe_path(realp);

        return c;
}

bool cbm_is_sysconfig_sane(SystemConfig *config)
{
        if (!config) {
                LOG_FATAL("sysconfig insane: Missing config");
                return false;
        }
        if (!config->root_device) {
                LOG_FATAL("sysconfig insane: Missing root device");
                return false;
        }
        return true;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
