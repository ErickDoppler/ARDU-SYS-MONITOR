#include "SettingsDialog.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "Autostart.h"
#include "IconFactory.h"
#include "SerialPort.h"
#include "resource.h"

namespace {

struct DialogState {
    Settings *settings;
    const DialogStatus *status;
    std::vector<std::wstring> portDevices;  // parallel to the combo, minus AUTO
    HICON icon;
};

void fillPortCombo(HWND dlg, DialogState &st) {
    HWND combo = GetDlgItem(dlg, IDC_PORT_COMBO);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    st.portDevices.clear();

    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"AUTO  (detect the board)"));

    int selected = 0;
    for (const PortInfo &p : enumerateSerialPorts()) {
        // Show the friendly name when Windows has one -- "COM20" alone is no help
        // when several adapters are plugged in.
        std::wstring label = p.device;
        if (!p.description.empty()) {
            label += L"  -  ";
            label += p.description;
        }
        const int index = static_cast<int>(
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        st.portDevices.push_back(p.device);
        if (!st.settings->portIsAuto() && p.device == st.settings->port) {
            selected = index;
        }
    }

    // A port that is configured but absent right now must not silently become
    // AUTO: add it back so Apply does not change the user's choice behind them.
    if (!st.settings->portIsAuto() && selected == 0) {
        std::wstring label = st.settings->port + L"  -  not present";
        const int index = static_cast<int>(
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        st.portDevices.push_back(st.settings->port);
        selected = index;
    }

    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

void fillIntervalCombo(HWND dlg, const Settings &s) {
    HWND combo = GetDlgItem(dlg, IDC_INTERVAL_COMBO);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    int selected = 0;
    const auto &choices = pollIntervalChoices();
    for (size_t i = 0; i < choices.size(); i++) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%d seconds", choices[i]);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        if (choices[i] == s.pollIntervalSec) selected = static_cast<int>(i);
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}

void fillStatus(HWND dlg, const DialogStatus &st) {
    std::wstring line = st.connected ? L"Link: connected" : L"Link: no board detected";
    if (!st.activePort.empty()) {
        line += L"   (";
        line += st.activePort;
        line += L")";
    }
    SetDlgItemTextW(dlg, IDC_STATUS_TEXT, line.c_str());
    SetDlgItemTextW(dlg, IDC_SOURCES_TEXT, st.sourceNote.c_str());
}

INT_PTR CALLBACK dialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto *st = reinterpret_cast<DialogState *>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            st = reinterpret_cast<DialogState *>(lp);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

            st->icon = icons::createAppIcon(32);
            if (st->icon) {
                SendMessageW(dlg, WM_SETICON, ICON_BIG,
                             reinterpret_cast<LPARAM>(st->icon));
                SendMessageW(dlg, WM_SETICON, ICON_SMALL,
                             reinterpret_cast<LPARAM>(st->icon));
            }

            // Reflect the real state of the scheduled task rather than the stored
            // preference: the two can diverge if the task was removed by hand.
            const bool taskExists = autostart::isEnabled();
            CheckDlgButton(dlg, IDC_AUTOSTART, taskExists ? BST_CHECKED : BST_UNCHECKED);

            fillPortCombo(dlg, *st);
            fillIntervalCombo(dlg, *st->settings);
            fillStatus(dlg, *st->status);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK: {
                    if (!st) return TRUE;

                    const int portIndex = static_cast<int>(
                        SendMessageW(GetDlgItem(dlg, IDC_PORT_COMBO), CB_GETCURSEL, 0, 0));
                    if (portIndex <= 0) {
                        st->settings->port = kPortAuto;
                    } else {
                        const size_t i = static_cast<size_t>(portIndex - 1);
                        if (i < st->portDevices.size()) st->settings->port = st->portDevices[i];
                    }

                    const int intervalIndex = static_cast<int>(SendMessageW(
                        GetDlgItem(dlg, IDC_INTERVAL_COMBO), CB_GETCURSEL, 0, 0));
                    const auto &choices = pollIntervalChoices();
                    if (intervalIndex >= 0 &&
                        static_cast<size_t>(intervalIndex) < choices.size()) {
                        st->settings->pollIntervalSec = choices[static_cast<size_t>(intervalIndex)];
                    }

                    const bool wantAutostart =
                        IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;

                    // Apply it now and report a failure, instead of storing a
                    // preference that never took effect.
                    if (wantAutostart != autostart::isEnabled()) {
                        std::wstring error;
                        const bool ok = wantAutostart ? autostart::enable(error)
                                                      : autostart::disable(error);
                        if (!ok) {
                            std::wstring msg =
                                L"The autostart entry could not be changed.\n\n" + error +
                                L"\n\nOther settings will still be applied.";
                            MessageBoxW(dlg, msg.c_str(), L"System Monitor",
                                        MB_ICONWARNING | MB_OK);
                        }
                    }
                    st->settings->autostart = autostart::isEnabled();

                    EndDialog(dlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            break;

        case WM_DESTROY:
            if (st && st->icon) {
                DestroyIcon(st->icon);
                st->icon = nullptr;
            }
            break;

        default:
            break;
    }
    return FALSE;
}

}  // namespace

bool showSettingsDialog(HWND owner, Settings &settings, const DialogStatus &status) {
    DialogState st{};
    st.settings = &settings;
    st.status = &status;
    st.icon = nullptr;

    const INT_PTR rc = DialogBoxParamW(GetModuleHandleW(nullptr),
                                       MAKEINTRESOURCEW(IDD_SETTINGS), owner, dialogProc,
                                       reinterpret_cast<LPARAM>(&st));
    return rc == IDOK;
}
