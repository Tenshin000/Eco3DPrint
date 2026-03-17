import os
import json
import tkinter as tk
from tkinter import messagebox, ttk
from PIL import Image, ImageTk

from utility.Log import Log

class PrinterScreen:
    """Module handling the 3D Printer dashboard interface and logic."""
    def __init__(self, parent_frame, root_window, send_ws_callback, logger, colors):
        self.parent = parent_frame
        self.root = root_window
        self.send_ws_message = send_ws_callback
        self.COLORS = colors

        self.logger = Log(logger_name="printer_logger", module_name="PRINTERS").get_logger()
        self.logger.info("Printer Screen initialized.")

        self.displayed_printers = {}
        self.card_images = {}
        self.target_printer_ip = None

        # SCROLLABLE CONTAINER SETUP
        # Create a Canvas
        self.canvas = tk.Canvas(self.parent, bg=self.COLORS["bg"], highlightthickness=0)
        # Create a Scrollbar linked to the Canvas
        self.scrollbar = ttk.Scrollbar(self.parent, orient=tk.VERTICAL, command=self.canvas.yview)
        # Create the Main Frame INSIDE the Canvas
        self.main_container = tk.Frame(self.canvas, bg=self.COLORS["bg"])

        self.canvas.configure(yscrollcommand=self.scrollbar.set)

        self.scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Add the Frame to a Canvas window anchored to Top-Left ("nw")
        self.canvas_window = self.canvas.create_window((0, 0), window=self.main_container, anchor="nw")

        # Bind events to update scroll region and recalculate grid on resize
        self.main_container.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", self._repack_cards)
        
        # Bind mousewheel scrolling only when the mouse is over the canvas
        self.canvas.bind("<Enter>", self._bind_mousewheel)
        self.canvas.bind("<Leave>", self._unbind_mousewheel)

    # MOUSEWHEEL SCROLLING LOGIC
    def _bind_mousewheel(self, event):
        """Binds the mouse wheel to the canvas for scrolling."""
        # Windows & Mac
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)
        # Linux
        self.canvas.bind_all("<Button-4>", self._on_mousewheel)
        self.canvas.bind_all("<Button-5>", self._on_mousewheel)

    def _unbind_mousewheel(self, event):
        """Unbinds the mouse wheel when leaving the canvas area."""
        self.canvas.unbind_all("<MouseWheel>")
        self.canvas.unbind_all("<Button-4>")
        self.canvas.unbind_all("<Button-5>")

    def _on_mousewheel(self, event):
        """Handles the actual scroll execution only if content exceeds the screen."""
        # Retrieve the current height of the Canvas and the frame containing the printers
        canvas_height = self.canvas.winfo_height()
        content_height = self.main_container.winfo_height()

        # If the printers fit comfortably on the screen, block scrolling
        if content_height <= canvas_height:
            return

        # If you need more lines, scroll instead
        if event.num == 5 or event.delta < 0:
            self.canvas.yview_scroll(1, "units")
        elif event.num == 4 or event.delta > 0:
            self.canvas.yview_scroll(-1, "units")

    def _repack_cards(self, event=None):
        """Calculates flow layout: arranges cards in a grid that wraps based on window width."""
        if not self.displayed_printers:
            return

        # Card width (200) + Total horizontal padding (15 left + 15 right = 30)
        card_width = 230
        canvas_width = self.canvas.winfo_width()
        
        # Prevent division by zero, ensure at least 1 column
        columns = max(1, canvas_width // card_width)
        
        # Sort printers alphabetically
        sorted_ips = sorted(self.displayed_printers.keys(), key=lambda k: self.displayed_printers[k]["data"]["name"].lower())
        
        # Place cards in the grid
        for index, ip in enumerate(sorted_ips):
            card = self.displayed_printers[ip]["card"]
            row = index // columns
            col = index % columns
            # Using grid instead of pack for wrap-around behavior
            card.grid(row=row, column=col, padx=15, pady=15, sticky="nw")

        # Force internal layout update before calculating scroll area
        self.parent.update_idletasks()
        # Strictly fix the scrollable area to the actual edges of the cards
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def process_incoming_data(self, json_payload):
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

        # Call the dynamic grid packing
        self._repack_cards()

    def _apply_color_filter(self, image_path, tint_color):
        """Loads an image, resizes it, and applies a semi-transparent color overlay."""
        try:
            base_dir = os.path.dirname(__file__)
            full_path = os.path.join(base_dir, "img", image_path)

            img = Image.open(full_path).convert("RGBA")

            try:
                resample_method = Image.Resampling.LANCZOS
            except AttributeError:
                resample_method = Image.LANCZOS

            img = img.resize((100, 100), resample_method)
            overlay = Image.new('RGBA', img.size, tint_color)
            mask = img.split()[3]
            overlay.putalpha(mask)
            tinted_img = Image.blend(img, overlay, alpha=0.4)

            return ImageTk.PhotoImage(tinted_img)

        except Exception as e:
            self.logger.error(f"Image processing failed for {image_path}: {e}")
            return None

    def _handle_card_click(self, ip):
        """Handles the user click event on a specific printer card."""
        if ip in self.displayed_printers:
            latest_data = self.displayed_printers[ip]["data"]
            self.show_printer_details(latest_data)

    def _create_printer_card(self, ip, name, node_type, utilization, status):
        """Instantiates and renders a new graphical card representing a specific 3D printer node."""
        # Frame size is set, grid layout will handle the placement
        card = tk.Frame(self.main_container, bg=self.COLORS["card_bg"], width=200, height=250, cursor="hand2", bd=2)
        card.pack_propagate(False)

        img_filename = "MonkeyFabSpire.png" if str(node_type).strip().lower() == "filament" else "PhotonS.png"
        
        if status == "ONLINE":
            filter_color = self.COLORS["filter_online"]
        elif status == "PRINTING":
            filter_color = self.COLORS["filter_printing"]
        else:
            filter_color = self.COLORS["filter_offline"]
            
        tk_image = self._apply_color_filter(img_filename, filter_color)
        self.card_images[ip] = tk_image

        if tk_image:
            icon_label = tk.Label(card, image=tk_image, bg=self.COLORS["card_bg"])
        else:
            icon_label = tk.Label(card, text="PRINTER", font=("Helvetica", 20), bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"])
        icon_label.pack(pady=(20, 10))

        name_label = tk.Label(card, text=name, font=("Helvetica", 14, "bold"), bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"])
        name_label.pack()

        ip_label = tk.Label(card, text=ip, font=("Helvetica", 10), bg=self.COLORS["card_bg"], fg=self.COLORS["text_muted"])
        ip_label.pack()

        printer_data = {"ip": ip, "name": name, "type": node_type, "utilization": utilization, "status": status}

        for widget in [card, icon_label, name_label, ip_label]:
            widget.bind("<Button-1>", lambda e, target_ip=ip: self._handle_card_click(target_ip))
            widget.bind("<Enter>", lambda e, c=card: c.configure(bg=self.COLORS["card_hover"]))
            widget.bind("<Leave>", lambda e, c=card: c.configure(bg=self.COLORS["card_bg"]))

        self.displayed_printers[ip] = {"card": card, "icon_label": icon_label, "name_label": name_label, "data": printer_data}

    def _update_printer_card(self, ip, name, node_type, utilization, status):
        """Updates the visual properties and internal state of an existing printer card."""
        printer = self.displayed_printers[ip]
        
        if printer["data"]["status"] != status or printer["data"]["name"] != name or printer["data"]["utilization"] != utilization:
            if status == "ONLINE":
                filter_color = self.COLORS["filter_online"]
            elif status == "PRINTING":
                filter_color = self.COLORS["filter_printing"]
            else:
                filter_color = self.COLORS["filter_offline"]
                
            img_filename = "MonkeyFabSpire.png" if str(node_type).strip().lower() == "filament" else "PhotonS.png"
            tk_image = self._apply_color_filter(img_filename, filter_color)
            self.card_images[ip] = tk_image
            
            if tk_image: printer["icon_label"].config(image=tk_image)
            printer["name_label"].config(text=name)
            
            printer["data"]["name"] = name
            printer["data"]["type"] = node_type
            printer["data"]["utilization"] = utilization
            printer["data"]["status"] = status

    def _add_detail_row(self, parent_frame, label, value):
        """Appends a standardized, horizontally-aligned key-value row."""
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
        if hasattr(self, '_modal_bg') and self._modal_bg.winfo_exists():
            self._modal_bg.destroy()

        self._modal_bg = tk.Frame(self.root, bg="#111111")
        self._modal_bg.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._modal_bg.bind("<Button-1>", lambda e: "break")

        popup = tk.Frame(self._modal_bg, bg=self.COLORS["bg"], highlightbackground=self.COLORS["header"], highlightthickness=2)
        popup.place(relx=0.5, rely=0.5, anchor="center", width=450, height=450)

        tk.Label(popup, text=f"{data.get('name', 'N/A')} Status", font=("Helvetica", 18, "bold"), bg=self.COLORS["bg"], fg=self.COLORS["header"]).pack(pady=20)

        details_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        details_frame.pack(fill=tk.BOTH, expand=True, padx=40)

        self._add_detail_row(details_frame, "IP Address", data.get('ip', 'N/A'))
        self._add_detail_row(details_frame, "Name", data.get('name', 'N/A'))
        self._add_detail_row(details_frame, "Type", data.get('type', 'N/A'))
        self._add_detail_row(details_frame, "Utilization", data.get('utilization', 'N/A'))
        self._add_detail_row(details_frame, "Status", data.get('status', 'N/A'))

        btn_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        btn_frame.pack(pady=20, fill=tk.X, padx=40)

        upload_btn = tk.Button(
            btn_frame,
            text="Queue STL" if data.get('status') in ["PRINTING", "OFFLINE"] else "Upload STL",
            font=("Helvetica", 12, "bold"),
            bg=self.COLORS["button_bg"],
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
        if hasattr(self, '_modal_bg') and self._modal_bg.winfo_exists():
            self._modal_bg.destroy()

        self._modal_bg = tk.Frame(self.root, bg="#111111")
        self._modal_bg.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._modal_bg.bind("<Button-1>", lambda e: "break")

        popup = tk.Frame(self._modal_bg, bg=self.COLORS["bg"], highlightbackground=self.COLORS["button_bg"], highlightthickness=2)
        popup.place(relx=0.5, rely=0.5, anchor="center", width=400, height=450)

        tk.Label(popup, text=f"Select STL for {self.target_printer_ip}", font=("Helvetica", 16, "bold"), bg=self.COLORS["bg"], fg=self.COLORS["button_bg"]).pack(pady=20)

        list_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        list_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        if not stl_files:
            tk.Label(list_frame, text="No STL files found on backend.", font=("Helvetica", 12), bg=self.COLORS["bg"], fg=self.COLORS["text_muted"]).pack(pady=40)
        else:
            listbox = tk.Listbox(
                list_frame, bg=self.COLORS["card_bg"], fg=self.COLORS["text_light"],
                font=("Helvetica", 12), selectbackground=self.COLORS["button_bg"], relief=tk.FLAT, bd=0,
                highlightthickness=1, highlightbackground=self.COLORS["text_muted"]
            )
            listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

            scrollbar = tk.Scrollbar(list_frame, orient=tk.VERTICAL)
            scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            listbox.config(yscrollcommand=scrollbar.set)
            scrollbar.config(command=listbox.yview)

            for file in stl_files:
                listbox.insert(tk.END, file)

            self._current_listbox = listbox

        btn_frame = tk.Frame(popup, bg=self.COLORS["bg"])
        btn_frame.pack(pady=20, fill=tk.X, padx=20)

        send_btn = tk.Button(
            btn_frame, text="Send to Printer", font=("Helvetica", 12, "bold"),
            bg=self.COLORS["button_bg"], fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self._execute_stl_send
        )
        send_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 10), ipady=5)
        
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
        
        payload = {
            "action": "send_stl",
            "ip": ip,
            "file": selected_file
        }
        
        self.send_ws_message(payload)
        self.logger.info(f"Instruction sent to backend: send '{selected_file}' to {ip}.")
        
        messagebox.showinfo("Upload Initiated", f"Command sent to server to transfer '{selected_file}'.")
        self._modal_bg.destroy()
    