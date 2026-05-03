/* rc-tivo.c - Keytable for TiVo remotes
 *
 * Copyright (c) 2011 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Initial mapping is for the TiVo remote included in the Nero LiquidTV bundle,
 * which also ships with a TiVo-branded IR transceiver, supported by the mceusb
 * driver. Note that the remote uses an NEC-ish protocol, but instead of having
 * a command/not_command pair, it has a vendor ID of 0xa10c, but some keys, the
 * NEC extended checksums do pass, so the table presently has the intended
 * values and the checksum-passed versions for those keys.
 */
static struct rc_map_table tivo[] = {
    /* vixs remote */
    { KEY_1,           KEY_1           },
    { KEY_2,           KEY_2           },
    { KEY_3,           KEY_3           },
    { KEY_4,           KEY_4           },
    { KEY_5,           KEY_5           },
    { KEY_6,           KEY_6           },
    { KEY_7,           KEY_7           },
    { KEY_8,           KEY_8           },
    { KEY_9,           KEY_9           },
    { KEY_0,           KEY_0           },
    { KEY_BACKSPACE,   KEY_BACKSPACE   },
    { KEY_ENTER,       KEY_ENTER       },
    { KEY_HOME,        KEY_HOME        },
    { KEY_UP,          KEY_UP          },
    { KEY_LEFT,        KEY_LEFT        },
    { KEY_RIGHT,       KEY_RIGHT       },
    { KEY_DOWN,        KEY_DOWN        },
    { KEY_MUTE,        KEY_MUTE        },
    { KEY_VOLUMEDOWN,  KEY_VOLUMEDOWN  },
    { KEY_VOLUMEUP,    KEY_VOLUMEUP    },
    { KEY_POWER,       KEY_POWER       },
    { KEY_MENU,        KEY_MENU        },
    { KEY_WWW,         KEY_WWW         },
    { KEY_EXIT,        KEY_EXIT        },    
    { KEY_ESC,         KEY_ESC         },
    { KEY_TAB,         KEY_TAB         },
    { KEY_SELECT,      KEY_SELECT      },
    { KEY_OK,          KEY_OK          },
    { KEY_AGAIN,       KEY_AGAIN       },
    /* customer remote - NEC protocol */
    { 0x708F10EF,      KEY_POWER       },
    { 0x708F41BE,      KEY_HOME        },
    { 0x708F0AF5,      KEY_MENU        },
    { 0x708F46B9,      KEY_MUTE        },
    { 0x708F12ED,      KEY_UP          },
    { 0x708F13EC,      KEY_DOWN        },
    { 0x708F14EB,      KEY_LEFT        },
    { 0x708F15EA,      KEY_RIGHT       },
    { 0x708F16E9,      KEY_SELECT      },
    { 0x708F42BD,      KEY_PLAYPAUSE   },
    { 0x708F40BF,      KEY_REWIND      },
    { 0x708F44BB,      KEY_FASTFORWARD },
    { 0x708F45BA,      KEY_STOP        },
    { 0x708F17E8,      KEY_ESC         },
    { 0x708F0EF1,      KEY_VOLUMEDOWN  },
    { 0x708F0FF0,      KEY_VOLUMEUP    },
};

static struct rc_map_list tivo_map = {
	.map = {
		.scan    = tivo,
		.size    = ARRAY_SIZE(tivo),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_TIVO,
	}
};

static int __init init_rc_map_tivo(void)
{
	return rc_map_register(&tivo_map);
}

static void __exit exit_rc_map_tivo(void)
{
	rc_map_unregister(&tivo_map);
}

module_init(init_rc_map_tivo)
module_exit(exit_rc_map_tivo)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
