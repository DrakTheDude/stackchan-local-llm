/*
 * One reader for the USB serial console, shared by everything that takes a
 * typed command.
 *
 * 🔴 IT HAS TO BE SHARED. `usb_serial_jtag_driver_install` succeeds once and
 *    returns ESP_ERR_INVALID_STATE afterwards, so a second feature that
 *    installed its own reader would not fail loudly - it would log a line
 *    nobody reads and then silently never receive a character. The first
 *    version of this WAS owned by the status source, and the moment a second
 *    command was wanted the ownership had to move here.
 *
 * 🔴 Reads the USB Serial JTAG peripheral DIRECTLY, not stdin.
 *
 *    This was fgets(stdin) first, and it could never have worked:
 *
 *      CONFIG_ESP_CONSOLE_UART_DEFAULT=y                 <- stdin is UART0
 *      CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y    <- USB only mirrors
 *                                                           the log
 *
 *    So the log comes out over USB while stdin is bound to UART0, which is not
 *    connected to anything here. Nothing drained the USB receive FIFO, so the
 *    host's write filled it and then BLOCKED FOREVER - which is also what kept
 *    leaving the serial port wedged and needing a physical replug.
 *
 *    Installing the driver and reading the peripheral directly avoids touching
 *    the console configuration at all, so logging keeps working exactly as it
 *    did. Changing the primary console to USB would also have fixed it, and
 *    would have moved the one diagnostic channel this project depends on.
 *
 * ⚠️ Commands run on the console's own task, which has a small stack and must
 *    not block. Anything slow or LVGL-shaped belongs on a task of its own.
 */
#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <functional>
#include <string>
#include <vector>

class SerialConsole {
public:
    // Called with the whole trimmed line, prefix included.
    using Handler = std::function<void(const std::string& line)>;

    // Registers a command. `prefix` is matched at the start of the line, so
    // "STATUS_" claims every STATUS_* command with one entry. `help` is one
    // short line, printed by HELP.
    //
    // Safe to call before Start(); registrations are kept either way.
    static void Register(const char* prefix, const char* help, Handler handler);

    // Installs the driver and starts reading. Idempotent - the second call does
    // nothing, which is what makes the ordering between features irrelevant.
    static void Start();

private:
    struct Command {
        std::string prefix;
        std::string help;
        Handler handler;
    };

    static void Task(void*);
    static void Dispatch(const std::string& line);
    static std::vector<Command>& Commands();
};

#endif  // SERIAL_CONSOLE_H
