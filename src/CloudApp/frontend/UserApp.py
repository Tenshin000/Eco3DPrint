import os
import asyncio
import json
import queue
import threading
import tkinter as tk
import websockets

from tkinter import ttk, messagebox
from PIL import Image, ImageTk

from utility.Log import Log

class UserApp:
    """Frontend User Application for managing 3D printers via WebSockets."""
    
    def __init__(self, root):
        """Initializes the main application window, variables, and background threads."""
        # Reference to the main Tkinter window
        self.root = root

        # Configure main window properties
        self.root.title("Eco3DPrint - Dashboard")
        self.root.geometry("900x600")
        self.root.configure(bg="#1E1E1E")

        # Initialize application logger
        self.logger = Log(logger_name="gui_logger", module_name="FRONTEND").get_logger()

        # Backend WebSocket address
        self.ws_url = "ws://localhost:8765"

        # Centralized color palette used by the UI
        self.COLORS = {
            "bg": "#1E1E1E",
            "header": "#2E7D32",
            "card_bg": "#2D2D2D",
            "card_hover": "#3D3D3D",
            "text_light": "#FFFFFF",
            "text_muted": "#AAAAAA",
            "filter_online": "#00FF00",
            "filter_offline": "#FF0000",
            "filter_printing": "#FFFF00" 
        }

        # Stores currently displayed printers indexed by their IP address
        self.displayed_printers = {}

        # Stores Tkinter image references to prevent Python's garbage collection from deleting them
        self.card_images = {}

        # Flag to handle clean application shutdown
        self.is_running = True

        # Thread-safe queue to pass data from the asyncio WebSocket thread to the Tkinter main thread
        self.update_queue = queue.Queue()
        
        # Variables to hold the WebSocket connection state for sending outgoing messages
        self.ws_loop = None
        self.current_ws = None
        
        # Keeps track of the IP address when initiating an STL file upload
        self.target_printer_ip = None

        # Build static interface components
        self._build_ui()

        # Start periodic checking of the thread-safe queue for incoming data
        self._process_queue()

        # Start WebSocket listening in a background thread
        self.logger.info("Starting WebSocket background thread...")
        self._start_websocket_thread()

    def _build_ui(self):
        """Constructs the static structural components of the user interface."""
        # Header bar at the top of the window
        header = tk.Frame(self.root, bg=self.COLORS["header"], height=70)
        header.pack(fill=tk.X, side=tk.TOP)
        header.pack_propagate(False)

        # Application title label
        title = tk.Label(
            header,
            text="Eco3DPrint Remote Management",
            bg=self.COLORS["header"],
            fg=self.COLORS["text_light"],
            font=("Helvetica", 20, "bold")
        )
        title.pack(side=tk.LEFT, padx=20, pady=15)

        # Main container where dynamically generated printer cards will be placed
        self.main_container = tk.Frame(self.root, bg=self.COLORS["bg"])
        self.main_container.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)

    def _apply_color_filter(self, image_path, tint_color):
        """
        Loads an image, resizes it, and applies a semi-transparent color overlay.
        Returns a Tkinter-compatible PhotoImage object.
        """
        try:
            base_dir = os.path.dirname(__file__)
            full_path = os.path.join(base_dir, "img", image_path)

            img = Image.open(full_path).convert("RGBA")

            # Handle compatibility for different Pillow library versions
            try:
                resample_method = Image.Resampling.LANCZOS
            except AttributeError:
                resample_method = Image.LANCZOS

            # Resize and apply the requested color tint
            img = img.resize((100, 100), resample_method)
            overlay = Image.new('RGBA', img.size, tint_color)
            mask = img.split()[3]
            overlay.putalpha(mask)
            tinted_img = Image.blend(img, overlay, alpha=0.4)

            return ImageTk.PhotoImage(tinted_img)

        except Exception as e:
            self.logger.error(f"Image processing failed for {image_path}: {e}")
            return None

    def _start_websocket_thread(self):
        """Spawns a daemon thread to execute the asyncio WebSocket loop asynchronously."""
        ws_thread = threading.Thread(target=self._run_async_websocket, daemon=True)
        ws_thread.start()

    def _run_async_websocket(self):
        """Initializes and executes the asyncio event loop for the WebSocket client."""
        self.ws_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.ws_loop)
        self.ws_loop.run_until_complete(self._websocket_listen())

    async def _websocket_listen(self):
        """Establishes a connection to the backend WebSocket and listens for push updates."""
        self.logger.info(f"Connecting to WebSocket at {self.ws_url}...")
        while self.is_running:
            try:
                # Attempt to establish WebSocket connection
                async with websockets.connect(self.ws_url) as ws:
                    self.logger.info("WebSocket connected successfully!")
                    self.current_ws = ws # Store active connection for sending outgoing requests
                    
                    # Continuously listen for incoming messages from the server
                    while self.is_running:
                        message = await ws.recv()
                        self.logger.debug(f"Received WS update: {message}")
                        
                        # Place the raw message into the queue for Tkinter's main thread to process
                        self.update_queue.put(message)
                        
            except websockets.exceptions.ConnectionClosed:
                self.logger.warning("WebSocket connection closed. Reconnecting in 5s...")
                self.current_ws = None
                await asyncio.sleep(5)
            except Exception as e:
                self.logger.error(f"WebSocket error: {e}")
                self.current_ws = None
                await asyncio.sleep(5)

    def send_ws_message(self, message_dict):
        """Thread-safe method to serialize and send a JSON request to the backend server."""
        if self.current_ws and self.ws_loop:
            coro = self.current_ws.send(json.dumps(message_dict))
            asyncio.run_coroutine_threadsafe(coro, self.ws_loop)
            self.logger.info(f"Sent request to backend: {message_dict}")
        else:
            self.logger.error("Cannot send message: WebSocket is disconnected.")
            messagebox.showerror("Network Error", "Not connected to the backend server.")

    def _process_queue(self):
        """Periodically checks the queue for new telemetry data and safely updates the UI."""
        if not self.is_running:
            return

        try:
            # Process all pending messages currently in the queue
            while True:
                message = self.update_queue.get_nowait()
                self._update_ui_with_data(message)
        except queue.Empty:
            pass # No new messages; this is completely normal behavior

        # Schedule the next queue check in 100 milliseconds
        self.root.after(100, self._process_queue)

    def _update_ui_with_data(self, json_payload):
        """Parses the incoming JSON payload and translates it into UI updates."""
        try:
            parsed_data = json.loads(json_payload)
            
            # Intercept custom backend responses (e.g., the requested STL directory list)
            if "type" in parsed_data and parsed_data["type"] == "stl_list":
                self.show_stl_selector(parsed_data.get("files", []))
                return
            
            # Handle standard error reporting from the backend
            if "error" in parsed_data:
                self.logger.error(f"Backend reported error: {parsed_data['error']}")
                return
                
            active_nodes = parsed_data
            
        except json.JSONDecodeError:
            self.logger.error("Failed to parse JSON from WebSocket message.")
            return

        current_ips = list(active_nodes.keys())

        # Iterate through the received nodes to either create or update their corresponding UI cards
        for ip, node_data in active_nodes.items():
            name = node_data.get("name", "Unknown")
            node_type = node_data.get("type", "Unknown")
            utilization = node_data.get("utilization", "Idle")
            status = node_data.get("status", "OFFLINE")

            if ip not in self.displayed_printers:
                self._create_printer_card(ip, name, node_type, utilization, status)
            else:
                self._update_printer_card(ip, name, node_type, utilization, status)

        # Remove cards for printers that are no longer reported by the backend
        for ip in list(self.displayed_printers.keys()):
            if ip not in current_ips:
                self.displayed_printers[ip]["card"].destroy()
                del self.displayed_printers[ip]
                if ip in self.card_images:
                    del self.card_images[ip]

    def _handle_card_click(self, ip):
        """Handles the user click event on a specific printer card, opening its detail view."""
        if ip in self.displayed_printers:
            latest_data = self.displayed_printers[ip]["data"]
            self.show_printer_details(latest_data)

    def _create_printer_card(self, ip, name, node_type, utilization, status):
        """Instantiates and renders a new graphical card representing a specific 3D printer node."""
        card = tk.Frame(self.main_container, bg=self.COLORS["card_bg"], width=200, height=250, cursor="hand2", bd=2)
        card.pack(side=tk.LEFT, padx=15, pady=15)
        card.pack_propagate(False)

        # Select the appropriate base image based on the printer type
        img_filename = "MonkeyFabSpire.png" if str(node_type).strip().lower() == "filament" else "PhotonS.png"
        
        # Determine the visual filter color based on the current operational status
        if status == "ONLINE":
            filter_color = self.COLORS["filter_online"]
        elif status == "PRINTING":
            filter_color = self.COLORS["filter_printing"]
        else:
            filter_color = self.COLORS["filter_offline"]
            
        tk_image = self._apply_color_filter(img_filename, filter_color)
        self.card_images[ip] = tk_image

        # Render the image icon or a text fallback if the image fails to load
        if tk_image:
            icon_label = tk.Label(card, image=tk_image, bg=self.COLORS["card_bg"])
        else:
            icon_label = tk.Label(card, text="PRINTER", font=("Helvetica", 20), bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"])
        icon_label.pack(pady=(20, 10))

        # Render textual metadata on the card
        name_label = tk.Label(card, text=name, font=("Helvetica", 14, "bold"), bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"])
        name_label.pack()

        ip_label = tk.Label(card, text=ip, font=("Helvetica", 10), bg=self.COLORS["card_bg"], fg=self.COLORS["text_muted"])
        ip_label.pack()

        # Cache the current data state to verify against future updates
        printer_data = {"ip": ip, "name": name, "type": node_type, "utilization": utilization, "status": status}

        # Bind hover and click events to the card and its children elements
        for widget in [card, icon_label, name_label, ip_label]:
            widget.bind("<Button-1>", lambda e, target_ip=ip: self._handle_card_click(target_ip))
            widget.bind("<Enter>", lambda e, c=card: c.configure(bg=self.COLORS["card_hover"]))
            widget.bind("<Leave>", lambda e, c=card: c.configure(bg=self.COLORS["card_bg"]))

        # Store the complete element in the active dictionary
        self.displayed_printers[ip] = {"card": card, "icon_label": icon_label, "name_label": name_label, "data": printer_data}

    def _update_printer_card(self, ip, name, node_type, utilization, status):
        """Updates the visual properties and internal state of an existing printer card upon receiving new telemetry."""
        printer = self.displayed_printers[ip]
        
        # Only update the UI if the relevant data attributes have actually changed
        if printer["data"]["status"] != status or printer["data"]["name"] != name or printer["data"]["utilization"] != utilization:
            
            # Update the overlay filter color dynamically if the operational status has changed
            if status == "ONLINE":
                filter_color = self.COLORS["filter_online"]
            elif status == "PRINTING":
                filter_color = self.COLORS["filter_printing"]
            else:
                filter_color = self.COLORS["filter_offline"]
                
            img_filename = "MonkeyFabSpire.png" if str(node_type).strip().lower() == "filament" else "PhotonS.png"
            tk_image = self._apply_color_filter(img_filename, filter_color)
            self.card_images[ip] = tk_image
            
            # Apply updated properties to the Tkinter widgets
            if tk_image: printer["icon_label"].config(image=tk_image)
            printer["name_label"].config(text=name)
            
            # Synchronize the cached state dictionary
            printer["data"]["name"] = name
            printer["data"]["type"] = node_type
            printer["data"]["utilization"] = utilization
            printer["data"]["status"] = status

    def _add_detail_row(self, parent_frame, label, value):
        """Appends a standardized, horizontally-aligned key-value row to the provided parent frame."""
        row = tk.Frame(parent_frame, bg=self.COLORS["bg"])
        row.pack(fill=tk.X, pady=5)
        tk.Label(row, text=f"{label}:", font=("Helvetica", 12, "bold"), bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], width=10, anchor="w").pack(side=tk.LEFT)
        tk.Label(row, text=str(value), font=("Helvetica", 12), bg=self.COLORS["bg"], fg=self.COLORS["text_light"], anchor="w").pack(side=tk.LEFT)

    def _request_stl_list(self, ip):
        """Saves the target IP address and dispatches a request to the backend for available STL files."""
        self.target_printer_ip = ip
        self.logger.info(f"Requesting STL list for printer {ip}...")
        self.send_ws_message({"action": "get_stls"})

    def show_printer_details(self, data):
        """Renders a modal window presenting comprehensive telemetry and controls for a specific printer."""
        # Ensure previous modals are cleared before instantiating a new one
        if hasattr(self, '_modal_bg') and self._modal_bg.winfo_exists():
            self._modal_bg.destroy()

        # Create a semi-transparent background to block interactions with the main UI
        self._modal_bg = tk.Frame(self.root, bg="#111111")
        self._modal_bg.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._modal_bg.bind("<Button-1>", lambda e: "break")

        # Container for the modal content
        popup = tk.Frame(self._modal_bg, bg=self.COLORS["bg"], highlightbackground=self.COLORS["header"], highlightthickness=2)
        popup.place(relx=0.5, rely=0.5, anchor="center", width=450, height=450)

        tk.Label(popup, text=f"{data.get('name', 'N/A')} Status", font=("Helvetica", 18, "bold"), bg=self.COLORS["bg"], fg=self.COLORS["header"]).pack(pady=20)

        details_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        details_frame.pack(fill=tk.BOTH, expand=True, padx=40)

        # Populate the detail rows based on the current printer telemetry
        self._add_detail_row(details_frame, "IP Address", data.get('ip', 'N/A'))
        self._add_detail_row(details_frame, "Name", data.get('name', 'N/A'))
        self._add_detail_row(details_frame, "Type", data.get('type', 'N/A'))
        self._add_detail_row(details_frame, "Utilization", data.get('utilization', 'N/A'))
        self._add_detail_row(details_frame, "Status", data.get('status', 'N/A'))

        btn_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        btn_frame.pack(pady=20, fill=tk.X, padx=40)

        # Trigger the WebSocket STL request when clicked; adjust button text based on status
        upload_btn = tk.Button(
            btn_frame,
            text="Queue STL" if data.get('status') in ["PRINTING", "OFFLINE"] else "Upload STL",
            font=("Helvetica", 12, "bold"),
            bg="#005CBF",
            fg=self.COLORS["text_light"],
            relief=tk.FLAT,
            command=lambda: self._request_stl_list(data.get('ip'))
        )
        upload_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 10), ipady=5)

        close_btn = tk.Button(
            btn_frame,
            text="Close",
            font=("Helvetica", 12),
            bg=self.COLORS["card_hover"],
            fg=self.COLORS["text_light"],
            relief=tk.FLAT,
            command=self._modal_bg.destroy
        )
        close_btn.pack(side=tk.RIGHT, expand=True, fill=tk.X, padx=(10, 0), ipady=5)

    def show_stl_selector(self, stl_files):
        """Displays a modal interface allowing the user to select an STL from the backend's known directory."""
        # Destroy the existing detail modal to make room for the file selector
        if hasattr(self, '_modal_bg') and self._modal_bg.winfo_exists():
            self._modal_bg.destroy()

        self._modal_bg = tk.Frame(self.root, bg="#111111")
        self._modal_bg.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._modal_bg.bind("<Button-1>", lambda e: "break")

        # Blue highlight for the action-oriented modal
        popup = tk.Frame(self._modal_bg, bg=self.COLORS["bg"], highlightbackground="#005CBF", highlightthickness=2)
        popup.place(relx=0.5, rely=0.5, anchor="center", width=400, height=450)

        tk.Label(
            popup,
            text=f"Select STL for {self.target_printer_ip}",
            font=("Helvetica", 16, "bold"),
            bg=self.COLORS["bg"],
            fg="#005CBF"
        ).pack(pady=20)

        # Container for the interactive file listbox
        list_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        list_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        if not stl_files:
            tk.Label(list_frame, text="No STL files found on backend.", font=("Helvetica", 12), bg=self.COLORS["bg"], fg=self.COLORS["text_muted"]).pack(pady=40)
        else:
            listbox = tk.Listbox(
                list_frame, bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"],
                font=("Helvetica", 12), selectbackground="#005CBF", relief=tk.FLAT, bd=0,
                highlightthickness=1, highlightbackground=self.COLORS["text_muted"]
            )
            listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

            scrollbar = tk.Scrollbar(list_frame, orient=tk.VERTICAL)
            scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            listbox.config(yscrollcommand=scrollbar.set)
            scrollbar.config(command=listbox.yview)

            # Populate the listbox with available files
            for file in stl_files:
                listbox.insert(tk.END, file)

            self._current_listbox = listbox

        btn_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        btn_frame.pack(pady=20, fill=tk.X, padx=20)

        send_btn = tk.Button(
            btn_frame, text="Send to Printer", font=("Helvetica", 12, "bold"),
            bg="#005CBF", fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self._execute_stl_send
        )
        send_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 10), ipady=5)
        
        # Disable the send button if the file array is empty
        if not stl_files:
            send_btn.config(state=tk.DISABLED, bg=self.COLORS["card_hover"])

        cancel_btn = tk.Button(
            btn_frame, text="Cancel", font=("Helvetica", 12),
            bg=self.COLORS["card_hover"], fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self._modal_bg.destroy
        )
        cancel_btn.pack(side=tk.RIGHT, expand=True, fill=tk.X, padx=(10, 0), ipady=5)

    def _execute_stl_send(self):
        """Assembles the payload and dispatches the instruction to send the selected STL to the target node."""
        if not hasattr(self, '_current_listbox'):
            return
            
        selection = self._current_listbox.curselection()
        if not selection:
            messagebox.showwarning("Warning", "Please select an STL file first.")
            return
            
        selected_file = self._current_listbox.get(selection[0])
        ip = self.target_printer_ip
        
        # Construct the JSON payload containing the action, target node, and chosen file
        payload = {
            "action": "send_stl",
            "ip": ip,
            "file": selected_file
        }
        
        # Dispatch the command to the backend
        self.send_ws_message(payload)
        self.logger.info(f"Instruction sent to backend: send '{selected_file}' to {ip}.")
        
        messagebox.showinfo("Upload Initiated", f"Command sent to server to transfer '{selected_file}'.")
        
        # Close the modal
        self._modal_bg.destroy()