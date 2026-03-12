import os
import signal
import tkinter as tk

from frontend.UserApp import UserApp
from utility.Log import Log

logger = Log(logger_name="app_logger", module_name="APP").get_logger()

def main():
    logger.info("Starting Eco3DPrint Frontend Interface...")
    
    root = tk.Tk()
    app = UserApp(root)

    is_shutting_down = False

    def shutdown(event=None):
        nonlocal is_shutting_down
        if is_shutting_down:
            return
        
        is_shutting_down = True
        logger.info("Closing Application...")
        
        # Stop the polling loop in the UserApp
        app.is_running = False

        # Detroy the GUI
        try:
            root.quit()
            root.destroy()
        except Exception:
            pass
        
        logger.info("Frontend terminated cleanly.")
        
        # Force quit to clean up CoAPthon network threads
        os._exit(0)

    def handle_sigint(sig, frame):
        """Capture the CTRL+C (SIGINT) signal from the OS."""
        logger.warning("\nCTRL+C detected! Initiating safe shutdown...")
        root.after(0, shutdown)

    # Bind CTRL+C to our function
    signal.signal(signal.SIGINT, handle_sigint)
    
    # Link the 'X' to the top right of the window
    root.protocol("WM_DELETE_WINDOW", shutdown)

    def check_signals():
        """Wakes up the Tkinter mainloop to process OS signals."""
        if not is_shutting_down:
            root.after(500, check_signals)

    root.after(500, check_signals)
    
    try:
        root.mainloop()
    finally:
        if not is_shutting_down:
            shutdown()

if __name__ == "__main__":
    main()
    