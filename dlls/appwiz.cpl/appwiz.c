/*
 * Add/Remove Programs applet
 * Partially based on Wine Uninstaller
 *
 * Copyright 2000 Andreas Mohr
 * Copyright 2004 Hannu Valtonen
 * Copyright 2005 Jonathan Ernst
 * Copyright 2001-2002, 2008 Owen Rudge
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "config.h"
#include "wine/port.h"
#include "wine/unicode.h"
#include "wine/debug.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cpl.h>

#include "res.h"

WINE_DEFAULT_DEBUG_CHANNEL(appwizcpl);

/* define a maximum length for various buffers we use */
#define MAX_STRING_LEN    1024

typedef struct APPINFO {
    int id;

    LPWSTR title;
    LPWSTR path;

    LPWSTR icon;
    int iconIdx;

    LPWSTR publisher;
    LPWSTR version;

    HKEY regroot;
    WCHAR regkey[MAX_STRING_LEN];

    struct APPINFO *next;
} APPINFO;

static struct APPINFO *AppInfo = NULL;
static HINSTANCE hInst;

/* names of registry keys */
static const WCHAR BackSlashW[] = { '\\', 0 };
static const WCHAR DisplayNameW[] = {'D','i','s','p','l','a','y','N','a','m','e',0};
static const WCHAR DisplayIconW[] = {'D','i','s','p','l','a','y','I','c','o','n',0};
static const WCHAR DisplayVersionW[] = {'D','i','s','p','l','a','y','V','e','r',
    's','i','o','n',0};
static const WCHAR PublisherW[] = {'P','u','b','l','i','s','h','e','r',0};
static const WCHAR UninstallCommandlineW[] = {'U','n','i','n','s','t','a','l','l',
    'S','t','r','i','n','g',0};

static const WCHAR PathUninstallW[] = {
        'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
        'U','n','i','n','s','t','a','l','l',0 };

/******************************************************************************
 * Name       : DllMain
 * Description: Entry point for DLL file
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason,
                    LPVOID lpvReserved)
{
    TRACE("(%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            hInst = hinstDLL;
            break;
    }
    return TRUE;
}

/******************************************************************************
 * Name       : FreeAppInfo
 * Description: Frees memory used by an AppInfo structure, and any children.
 */
static void FreeAppInfo(APPINFO *info)
{
    APPINFO *iter = info;

    while (iter)
    {
        if (iter->title)
            HeapFree(GetProcessHeap(), 0, iter->title);

        if (iter->path)
            HeapFree(GetProcessHeap(), 0, iter->path);

        if (iter->icon)
            HeapFree(GetProcessHeap(), 0, iter->icon);

        if (iter->publisher)
            HeapFree(GetProcessHeap(), 0, iter->publisher);

        if (iter->version)
            HeapFree(GetProcessHeap(), 0, iter->version);

        iter = iter->next;
        HeapFree(GetProcessHeap(), 0, iter);
    }
}

/******************************************************************************
 * Name       : ReadApplicationsFromRegistry
 * Description: Creates a linked list of uninstallable applications from the
 *              registry.
 * Parameters : root    - Which registry root to read from (HKCU/HKLM)
 * Returns    : TRUE if successful, FALSE otherwise
 */
static BOOL ReadApplicationsFromRegistry(HKEY root)
{
    HKEY hkeyUninst, hkeyApp;
    int i, id = 0;
    DWORD sizeOfSubKeyName, displen, uninstlen;
    WCHAR subKeyName[256];
    WCHAR key_app[MAX_STRING_LEN];
    WCHAR *p;
    APPINFO *iter = AppInfo;
    LPWSTR iconPtr;
    BOOL ret = FALSE;

    if (RegOpenKeyExW(root, PathUninstallW, 0, KEY_READ, &hkeyUninst) !=
      ERROR_SUCCESS)
        return FALSE;

    lstrcpyW(key_app, PathUninstallW);
    lstrcatW(key_app, BackSlashW);
    p = key_app+lstrlenW(PathUninstallW)+1;

    sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);

    if (iter)
    {
        /* find the end of the list */
        for (iter = AppInfo; iter->next; iter = iter->next);
    }

    for (i = 0; RegEnumKeyExW(hkeyUninst, i, subKeyName, &sizeOfSubKeyName, NULL,
        NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS; ++i)
    {
        lstrcpyW(p, subKeyName);
        RegOpenKeyExW(root, key_app, 0, KEY_READ, &hkeyApp);

        displen = 0;
        uninstlen = 0;

        if ((RegQueryValueExW(hkeyApp, DisplayNameW, 0, 0, NULL, &displen) ==
            ERROR_SUCCESS) && (RegQueryValueExW(hkeyApp, UninstallCommandlineW,
            0, 0, NULL, &uninstlen) == ERROR_SUCCESS))
        {
            /* if we already have iter, allocate the next entry */
            if (iter)
            {
                iter->next = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    sizeof(struct APPINFO));

                if (!iter->next)
                    goto err;

                iter = iter->next;
            }
            else
            {
                /* if not, start the list */
                iter = AppInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    sizeof(struct APPINFO));

                if (!iter)
                    goto err;
            }

            iter->title = HeapAlloc(GetProcessHeap(), 0, displen);

            if (!iter->title)
                goto err;

            RegQueryValueExW(hkeyApp, DisplayNameW, 0, 0, (LPBYTE)iter->title,
                &displen);

            /* now get DisplayIcon */
            displen = 0;
            RegQueryValueExW(hkeyApp, DisplayIconW, 0, 0, NULL, &displen);

            if (displen == 0)
                iter->icon = 0;
            else
            {
                iter->icon = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!iter->icon)
                    goto err;

                RegQueryValueExW(hkeyApp, DisplayIconW, 0, 0, (LPBYTE)iter->icon,
                    &displen);

                /* separate the index from the icon name, if supplied */
                iconPtr = strchrW(iter->icon, ',');

                if (iconPtr)
                {
                    *iconPtr++ = 0;
                    iter->iconIdx = atoiW(iconPtr);
                }
            }

            iter->path = HeapAlloc(GetProcessHeap(), 0, uninstlen);

            if (!iter->path)
                goto err;

            RegQueryValueExW(hkeyApp, UninstallCommandlineW, 0, 0,
                (LPBYTE)iter->path, &uninstlen);

            /* publisher, version */
            if (RegQueryValueExW(hkeyApp, PublisherW, 0, 0, NULL, &displen) ==
                ERROR_SUCCESS)
            {
                iter->publisher = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!iter->publisher)
                    goto err;

                RegQueryValueExW(hkeyApp, PublisherW, 0, 0, (LPBYTE)iter->publisher,
                    &displen);
            }

            if (RegQueryValueExW(hkeyApp, DisplayVersionW, 0, 0, NULL, &displen) ==
                ERROR_SUCCESS)
            {
                iter->version = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!iter->version)
                    goto err;

                RegQueryValueExW(hkeyApp, DisplayVersionW, 0, 0, (LPBYTE)iter->version,
                    &displen);
            }

            /* registry key */
            iter->regroot = root;
            lstrcpyW(iter->regkey, subKeyName);

            iter->id = id++;
        }

        RegCloseKey(hkeyApp);
        sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);
    }

    ret = TRUE;
    goto end;

err:
    RegCloseKey(hkeyApp);
    FreeAppInfo(iter);

end:
    RegCloseKey(hkeyUninst);
    return ret;
}

/******************************************************************************
 * Name       : EmptyList
 * Description: Frees memory used by the application linked list.
 */
static inline void EmptyList(void)
{
    FreeAppInfo(AppInfo);
    AppInfo = NULL;
}

/******************************************************************************
 * Name       : UpdateButtons
 * Description: Enables/disables the Add/Remove button depending on current
 *              selection in list box.
 * Parameters : hWnd    - Handle of the dialog box
 */
static void UpdateButtons(HWND hWnd)
{
    BOOL sel = ListView_GetSelectedCount(GetDlgItem(hWnd, IDL_PROGRAMS)) != 0;

    EnableWindow(GetDlgItem(hWnd, IDC_ADDREMOVE), sel);
    EnableWindow(GetDlgItem(hWnd, IDC_SUPPORT_INFO), sel);
}

/* Definition of column headers for AddListViewColumns function */
typedef struct AppWizColumn {
   int width;
   int fmt;
   int title;
} AppWizColumn;

AppWizColumn columns[] = {
    {200, LVCFMT_LEFT, IDS_COLUMN_NAME},
    {150, LVCFMT_LEFT, IDS_COLUMN_PUBLISHER},
    {100, LVCFMT_LEFT, IDS_COLUMN_VERSION},
};

/******************************************************************************
 * Name       : AddListViewColumns
 * Description: Adds column headers to the list view control.
 * Parameters : hWnd    - Handle of the list view control.
 * Returns    : TRUE if completed successfully, FALSE otherwise.
 */
static BOOL AddListViewColumns(HWND hWnd)
{
    WCHAR buf[MAX_STRING_LEN];
    LVCOLUMNW lvc;
    int i;

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;

    /* Add the columns */
    for (i = 0; i < sizeof(columns) / sizeof(columns[0]); i++)
    {
        lvc.iSubItem = i;
        lvc.pszText = buf;

        /* set width and format */
        lvc.cx = columns[i].width;
        lvc.fmt = columns[i].fmt;

        LoadStringW(hInst, columns[i].title, buf, sizeof(buf) / sizeof(buf[0]));

        if (ListView_InsertColumnW(hWnd, i, &lvc) == -1)
            return FALSE;
    }

    return TRUE;
}

/******************************************************************************
 * Name       : AddListViewImageList
 * Description: Creates an ImageList for the list view control.
 * Parameters : hWnd    - Handle of the list view control.
 * Returns    : Handle of the image list.
 */
static HIMAGELIST AddListViewImageList(HWND hWnd)
{
    HIMAGELIST hSmall;
    HICON hDefaultIcon;

    hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        ILC_MASK, 1, 1);

    /* Add default icon to image list */
    hDefaultIcon = LoadIconW(hInst, MAKEINTRESOURCEW(ICO_MAIN));
    ImageList_AddIcon(hSmall, hDefaultIcon);
    DestroyIcon(hDefaultIcon);

    (void) ListView_SetImageList(hWnd, hSmall, LVSIL_SMALL);

    return hSmall;
}

/******************************************************************************
 * Name       : ResetApplicationList
 * Description: Empties the app list, if need be, and recreates it.
 * Parameters : bFirstRun  - TRUE if this is the first time this is run, FALSE otherwise
 *              hWnd       - handle of the dialog box
 *              hImageList - handle of the image list
 * Returns    : New handle of the image list.
 */
static HIMAGELIST ResetApplicationList(BOOL bFirstRun, HWND hWnd, HIMAGELIST hImageList)
{
    HWND hWndListView;

    hWndListView = GetDlgItem(hWnd, IDL_PROGRAMS);

    /* if first run, create the image list and add the listview columns */
    if (bFirstRun)
    {
        if (!AddListViewColumns(hWndListView))
            return NULL;
    }
    else /* we need to remove the existing things first */
    {
        ImageList_Destroy(hImageList);

        /* reset the list, since it's probably changed if the uninstallation was
           successful */
        EmptyList();
    }

    /* now create the image list and add the applications to the listview */
    hImageList = AddListViewImageList(hWndListView);

    ReadApplicationsFromRegistry(HKEY_LOCAL_MACHINE);
    ReadApplicationsFromRegistry(HKEY_CURRENT_USER);

    UpdateButtons(hWnd);

    return(hImageList);
}

/******************************************************************************
 * Name       : MainDlgProc
 * Description: Callback procedure for main tab
 * Parameters : hWnd    - hWnd of the window
 *              msg     - reason for calling function
 *              wParam  - additional parameter
 *              lParam  - additional parameter
 * Returns    : Dependant on message
 */
static BOOL CALLBACK MainDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HIMAGELIST hImageList;

    switch(msg)
    {
        case WM_INITDIALOG:
            hImageList = ResetApplicationList(TRUE, hWnd, hImageList);

            if (!hImageList)
                return FALSE;

            return TRUE;

        case WM_DESTROY:
            ImageList_Destroy(hImageList);

            EmptyList();

            return 0;
    }

    return FALSE;
}

/******************************************************************************
 * Name       : StartApplet
 * Description: Main routine for applet
 * Parameters : hWnd    - hWnd of the Control Panel
 */
static void StartApplet(HWND hWnd)
{
    PROPSHEETPAGEW psp;
    PROPSHEETHEADERW psh;
    WCHAR tab_title[MAX_STRING_LEN], app_title[MAX_STRING_LEN];

    /* Load the strings we will use */
    LoadStringW(hInst, IDS_TAB1_TITLE, tab_title, sizeof(tab_title) / sizeof(tab_title[0]));
    LoadStringW(hInst, IDS_CPL_TITLE, app_title, sizeof(app_title) / sizeof(app_title[0]));

    /* Fill out the PROPSHEETPAGE */
    psp.dwSize = sizeof (PROPSHEETPAGEW);
    psp.dwFlags = PSP_USETITLE;
    psp.hInstance = hInst;
    psp.pszTemplate = MAKEINTRESOURCEW (IDD_MAIN);
    psp.pszIcon = NULL;
    psp.pfnDlgProc = (DLGPROC) MainDlgProc;
    psp.pszTitle = tab_title;
    psp.lParam = 0;

    /* Fill out the PROPSHEETHEADER */
    psh.dwSize = sizeof (PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID;
    psh.hwndParent = hWnd;
    psh.hInstance = hInst;
    psh.pszIcon = NULL;
    psh.pszCaption = app_title;
    psh.nPages = 1;
    psh.ppsp = &psp;
    psh.pfnCallback = NULL;
    psh.nStartPage = 0;

    /* Display the property sheet */
    PropertySheetW (&psh);
}

/******************************************************************************
 * Name       : CPlApplet
 * Description: Entry point for Control Panel applets
 * Parameters : hwndCPL - hWnd of the Control Panel
 *              message - reason for calling function
 *              lParam1 - additional parameter
 *              lParam2 - additional parameter
 * Returns    : Dependant on message
 */
LONG CALLBACK CPlApplet(HWND hwndCPL, UINT message, LPARAM lParam1, LPARAM lParam2)
{
    INITCOMMONCONTROLSEX iccEx;

    switch (message)
    {
        case CPL_INIT:
            iccEx.dwSize = sizeof(iccEx);
            iccEx.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;

            InitCommonControlsEx(&iccEx);

            return TRUE;

        case CPL_GETCOUNT:
            return 1;

        case CPL_INQUIRE:
        {
            CPLINFO *appletInfo = (CPLINFO *) lParam2;

            appletInfo->idIcon = ICO_MAIN;
            appletInfo->idName = IDS_CPL_TITLE;
            appletInfo->idInfo = IDS_CPL_DESC;
            appletInfo->lData = 0;

            break;
        }

        case CPL_DBLCLK:
            StartApplet(hwndCPL);
            break;
    }

    return FALSE;
}
