/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#ifndef _OPLUS_HMBIRD_PARSE_DTS_H_
#define _OPLUS_HMBIRD_PARSE_DTS_H_
#include <linux/of.h>
#include <linux/string.h>
#include <linux/printk.h>

enum hmbird_dts_config {
        HMBIRD_UNINIT,
        HMBIRD_EXT,
        HMBIRD_UNKNOW,
};

static enum hmbird_dts_config hmbird_config_type = HMBIRD_UNINIT;

#define HMBIRD_CONFIG_TYPE_PATH "/soc/oplus,hmbird/config_type"

static inline enum hmbird_dts_config get_hmbird_config_type(void)
{
        struct device_node *np = NULL;
        const char *hmbird_config_str = NULL;
        if (HMBIRD_UNINIT != hmbird_config_type)
                return hmbird_config_type;
        np = of_find_node_by_path(HMBIRD_CONFIG_TYPE_PATH);
        if (np) {
                of_property_read_string(np, "type", &hmbird_config_str);
                if (NULL != hmbird_config_str) {
                        if (strncmp(hmbird_config_str, "HMBIRD_EXT", strlen("HMBIRD_EXT")) == 0) {
                                hmbird_config_type = HMBIRD_EXT;
                                pr_debug("hmbird config use HMBIRD_EXT, set by dtsi");
                        } else {
                                hmbird_config_type = HMBIRD_UNKNOW;
                                pr_debug("hmbird config use default HMBIRD_UNKNOW, set by dtsi");
                        }
                        return hmbird_config_type;
                }
        }

        hmbird_config_type = HMBIRD_UNKNOW;
        pr_debug("hmbird config use default HMBIRD_UNKNOW");
        return hmbird_config_type;
}

#endif /*_OPLUS_HMBIRD_PARSE_DTS_H_ */
