#pragma once

// Menus, accelerators and the main window's commands.
#define IDR_MAINMENU            100
#define IDR_ACCEL               101

#define IDM_CONNECT             1001
#define IDM_DISCONNECT          1002
#define IDM_RELOAD              1003
#define IDM_EXPORT_STATS        1004
#define IDM_EXPORT_LOG          1005
#define IDM_SERVICE_START       1006
#define IDM_SERVICE_STOP        1007
#define IDM_SERVICE_RESTART     1008
#define IDM_SERVICE_INSTALL     1009
#define IDM_EXIT                1010

#define IDM_ANSWER              1101
#define IDM_HANGUP              1102
#define IDM_RESET               1103
#define IDM_TOGGLE_SERVICE      1104
#define IDM_ALL_IN_SERVICE      1105
#define IDM_PROPERTIES          1106

#define IDM_REREGISTER          1201
#define IDM_UNREGISTER          1202
#define IDM_ACCOUNT_SETTINGS    1203
#define IDM_SIP_TRACE           1204

#define IDM_NEW_LINE            1301
#define IDM_REMOVE_LINE         1302
#define IDM_MODEM_DEFAULTS      1303
#define IDM_SELFTEST            1304

#define IDM_VIEW_TOOLBAR        1401
#define IDM_VIEW_STATUSBAR      1402
#define IDM_VIEW_LOG            1403
#define IDM_REFRESH             1404

#define IDM_SETTINGS            1501
#define IDM_AT_CONSOLE          1502
#define IDM_PORT_PAIRS          1503
#define IDM_DRIVERS             1504

#define IDM_HELP_CONTENTS       1601
#define IDM_README              1602
#define IDM_ABOUT               1603

#define IDM_STATUS_LABEL        1700

// Dialogs.
#define IDD_CONNECT             200
#define IDD_LINE                201
#define IDD_SETTINGS            202
#define IDD_SELFTEST            203
#define IDD_PORTS               204
#define IDD_TRACE               205
#define IDD_CONSOLE             206
#define IDD_ABOUT               207

// Connect.
#define IDC_CN_HOST             300
#define IDC_CN_PORT             301
#define IDC_CN_PASSWORD         302

// Line properties / new line.
#define IDC_LN_NAME             400
#define IDC_LN_COM              401
#define IDC_LN_EXT              402
#define IDC_LN_PASS             403
#define IDC_LN_SERVER           404
#define IDC_LN_MAX              405
#define IDC_LN_AUTO             406
#define IDC_LN_INSERVICE        407
#define IDC_LN_V42              408
#define IDC_LN_COMP             409
#define IDC_LN_DRIVER           410
#define IDC_LN_PASS_NOTE        411

// Settings: the tab control, and each page's controls in its own range
// (2000 + 100 * page), labels included, so a page is shown by ID range.
#define IDC_SET_TAB             1900
#define IDC_SET_FOOTER          1901
#define IDC_PAGE_FIRST          2000
#define IDC_PAGE_SPAN           100

#define IDC_SIP_SERVER          2001
#define IDC_SIP_PORT            2002
#define IDC_SIP_LOCALPORT       2003
#define IDC_SIP_TRANSPORT       2004
#define IDC_SIP_REALM           2005
#define IDC_SIP_EXPIRY          2006
#define IDC_SIP_REGONSTART      2007
#define IDC_SIP_L1              2051
#define IDC_SIP_L2              2052
#define IDC_SIP_L3              2053
#define IDC_SIP_L4              2054
#define IDC_SIP_L5              2055
#define IDC_SIP_L6              2056
#define IDC_SIP_NOTE            2057

#define IDC_MED_CODEC           2101
#define IDC_MED_RTP             2102
#define IDC_MED_JITTER          2103
#define IDC_MED_EC              2104
#define IDC_MED_L1              2151
#define IDC_MED_L2              2152
#define IDC_MED_L3              2153
#define IDC_MED_L4              2154
#define IDC_MED_PTIME           2155
#define IDC_MED_N1              2156
#define IDC_MED_N2              2157

#define IDC_MOD_MAX             2201
#define IDC_MOD_MIN             2202
#define IDC_MOD_RINGS           2203
#define IDC_MOD_V42             2204
#define IDC_MOD_COMP            2205
#define IDC_MOD_IDENT           2206
#define IDC_MOD_L1              2251
#define IDC_MOD_L2              2252
#define IDC_MOD_L3              2253
#define IDC_MOD_L4              2254
#define IDC_MOD_NOTE            2255

#define IDC_LOG_FILE            2301
#define IDC_LOG_LEVEL           2302
#define IDC_LOG_ROTATE          2303
#define IDC_LOG_KEEP            2304
#define IDC_LOG_TRACEAT         2305
#define IDC_LOG_L1              2351
#define IDC_LOG_L2              2352
#define IDC_LOG_L3              2353
#define IDC_LOG_L4              2354
#define IDC_LOG_N1              2355

#define IDC_CTL_PORT            2401
#define IDC_CTL_BIND            2402
#define IDC_CTL_PASSWORD        2403
#define IDC_CTL_CLEARPW         2404
#define IDC_CTL_L1              2451
#define IDC_CTL_L2              2452
#define IDC_CTL_L3              2453
#define IDC_CTL_N1              2454
#define IDC_CTL_N2              2455

// Self-test.
#define IDC_ST_MOD              500
#define IDC_ST_RUN              501
#define IDC_ST_RESULT           502

// Ports and drivers.
#define IDC_PT_PAIRS            600
#define IDC_PT_MODEMS           601
#define IDC_PT_REFRESH          602
#define IDC_PT_NOTE             603

// SIP trace.
#define IDC_TR_TEXT             700
#define IDC_TR_ALL              701
#define IDC_TR_DEBUG            702

// AT command console.
#define IDC_CON_PORT            800
#define IDC_CON_OPEN            801
#define IDC_CON_OUT             802
#define IDC_CON_IN              803
#define IDC_CON_SEND            804
#define IDC_CON_NOTE            805

// About.
#define IDC_AB_TEXT             900
