import json
import tkinter as tk

from tkinter import ttk
from utility.Log import Log

class DailyReportScreen:
    """Module handling the Daily Energy Report interface and logic."""
    def __init__(self, parent_frame, send_ws_callback, colors):
        """
        Initializes the Daily Report screen inside the given parent frame.
        
        :param parent_frame: The frame (tab) where this screen will be drawn.
        :param send_ws_callback: Function reference to send WebSocket messages.
        :param colors: Shared color palette dictionary.
        """
        self.parent = parent_frame
        self.send_ws_message = send_ws_callback
        self.COLORS = colors

        self.logger = Log(logger_name="report_logger", module_name="REPORT").get_logger()
        self.logger.info("Daily Report Screen initialized.")

        self._build_ui()

    def _build_ui(self):
        """Constructs the Daily Report interface."""
        # Top Bar for controls
        control_frame = tk.Frame(self.parent, bg=self.COLORS["bg"])
        control_frame.pack(fill=tk.X, padx=20, pady=10)

        refresh_btn = tk.Button(
            control_frame, text="Refresh Daily Report", font=("Helvetica", 12, "bold"),
            bg=self.COLORS["button_bg"], fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self.request_report
        )
        refresh_btn.pack(side=tk.RIGHT, ipady=5, ipadx=10)

        title_lbl = tk.Label(
            control_frame, text="Daily Energy Consumption Analysis", 
            bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 16, "bold")
        )
        title_lbl.pack(side=tk.LEFT)

        # Internal Notebook and Treeview Styles
        style = ttk.Style()
        style.theme_use('default')
        
        # Notebook Style
        style.configure('Inner.TNotebook', background=self.COLORS["bg"], borderwidth=0)
        style.configure('Inner.TNotebook.Tab', background=self.COLORS["card_bg"], foreground=self.COLORS["text_light"], padding=[10, 2])
        style.map('Inner.TNotebook.Tab', background=[('selected', self.COLORS["header"])])

        # Treeview (Table) Style
        style.configure("Treeview", 
                        background=self.COLORS["card_bg"],
                        foreground=self.COLORS["text_light"],
                        fieldbackground=self.COLORS["card_bg"],
                        rowheight=35, # Taller rows for better readability
                        font=('Helvetica', 11),
                        borderwidth=0)
        style.map("Treeview", background=[('selected', self.COLORS["button_bg"])])

        # Treeview Heading Style
        style.configure("Treeview.Heading", 
                        background=self.COLORS["header"], 
                        foreground=self.COLORS["text_light"], 
                        font=('Helvetica', 12, 'bold'),
                        borderwidth=1,
                        relief="flat")
        style.map("Treeview.Heading", background=[('active', self.COLORS["button_bg"])])

        self.inner_notebook = ttk.Notebook(self.parent, style='Inner.TNotebook')
        self.inner_notebook.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        # Tab 1: Global
        self.global_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["card_bg"])
        self.inner_notebook.add(self.global_frame, text=" Global Summary ")

        # Tab 2: By Printer
        self.printer_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["bg"])
        self.inner_notebook.add(self.printer_frame, text=" By Printer ")
        self.printer_tree = self._create_treeview(self.printer_frame, "Printer IP")

        # Tab 3: By STL
        self.stl_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["bg"])
        self.inner_notebook.add(self.stl_frame, text=" By STL ")
        self.stl_tree = self._create_treeview(self.stl_frame, "STL Name")

        # Initial prompt
        self.global_content = tk.Frame(self.global_frame, bg=self.COLORS["card_bg"])
        self.global_content.pack(expand=True, fill=tk.BOTH, padx=20, pady=20)
        
        tk.Label(
            self.global_content, 
            text="Fetching today's data...", 
            bg=self.COLORS["card_bg"], 
            fg=self.COLORS["text_muted"], 
            font=("Helvetica", 14)
        ).pack(pady=40)

    def _create_treeview(self, parent, first_col_name):
        """Creates a standardized Treeview for tabular data display."""
        columns = ("name", "total", "used", "wasted", "avg_print", "avg_succ", "avg_fail")
        
        tree = ttk.Treeview(parent, columns=columns, show="headings", height=15)
        
        # Configure column headings
        tree.heading("name", text=first_col_name)
        tree.heading("total", text="Total Energy")
        tree.heading("used", text="Well-Used (Finished)")
        tree.heading("wasted", text="Wasted (Failed/Error)")
        tree.heading("avg_print", text="Avg / Print")
        tree.heading("avg_succ", text="Avg / Success")
        tree.heading("avg_fail", text="Avg / Failed")
        
        # Configure column widths
        for col in columns:
            tree.column(col, anchor=tk.CENTER, width=120)
        tree.column("name", width=200, anchor=tk.W)

        # Configure tags for alternating row colors (Zebra Striping)
        tree.tag_configure('oddrow', background=self.COLORS["bg"])
        tree.tag_configure('evenrow', background=self.COLORS["card_bg"])

        # Scrollbar
        scrollbar = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=tree.yview)
        tree.configure(yscroll=scrollbar.set)
        
        tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        return tree

    def request_report(self):
        """Sends a WebSocket request to generate the daily report."""
        self.logger.info("Requesting Daily Report from backend...")
        self.send_ws_message({"action": "get_daily_report"})

    def process_incoming_data(self, json_payload):
        """Parses and renders the daily report data into the UI."""
        try:
            parsed_data = json.loads(json_payload)
            report = parsed_data.get("data", {})
            
            self._render_global(report.get("global", {}))
            self._render_table(self.printer_tree, report.get("by_printer", {}))
            self._render_table(self.stl_tree, report.get("by_stl", {}))
            
            self.logger.info("Daily Report rendered successfully.")
            
        except Exception as e:
            self.logger.error(f"Error processing report data: {e}")

    def _make_card(self, parent, title, value, color):
        frame = tk.Frame(parent, bg=self.COLORS["bg"], bd=1, relief=tk.RIDGE)
        frame.pack(side=tk.LEFT, padx=10, pady=5, fill=tk.BOTH, expand=True)
        tk.Label(frame, text=title, bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], font=("Helvetica", 11, "bold")).pack(pady=(15, 0))
        tk.Label(frame, text=value, bg=self.COLORS["bg"], fg=color, font=("Helvetica", 18, "bold")).pack(pady=(5, 15))

    def _render_global(self, global_data):
        """Draws the Global Summary dashboard cards."""
        # Clear existing content
        for widget in self.global_content.winfo_children():
            widget.destroy()

        if not global_data or global_data.get("prints_count", 0) == 0:
            tk.Label(
                self.global_content, 
                text="No prints recorded today.", 
                bg=self.COLORS["card_bg"], 
                fg=self.COLORS["text_muted"], 
                font=("Helvetica", 14)
            ).pack(pady=40)
            return

        # Top Section: Total Prints Centered
        top_row = tk.Frame(self.global_content, bg=self.COLORS["card_bg"])
        top_row.pack(fill=tk.X, pady=5)
        
        prints_frame = tk.Frame(top_row, bg=self.COLORS["bg"], bd=1, relief=tk.RIDGE)
        prints_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)
        tk.Label(prints_frame, text="Total Prints Today", bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], font=("Helvetica", 12, "bold")).pack(pady=(15, 0))
        tk.Label(prints_frame, text=str(global_data["prints_count"]), bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 24, "bold")).pack(pady=(5, 15))

        # Middle Section: Total Energies
        row1 = tk.Frame(self.global_content, bg=self.COLORS["card_bg"])
        row1.pack(fill=tk.X, pady=5)
        self._make_card(row1, "Total Energy", f"{global_data['total_energy']:.2f} J", "#00A2FF")
        self._make_card(row1, "Well-Used Energy", f"{global_data['used_energy']:.2f} J", self.COLORS["filter_online"])
        self._make_card(row1, "Wasted Energy", f"{global_data['wasted_energy']:.2f} J", self.COLORS["filter_offline"])

        # Bottom Section: Averages
        row2 = tk.Frame(self.global_content, bg=self.COLORS["card_bg"])
        row2.pack(fill=tk.X, pady=5)
        self._make_card(row2, "Avg Energy / Print", f"{global_data['avg_per_print']:.2f} J", "#00A2FF")
        self._make_card(row2, "Avg Energy / Success", f"{global_data['avg_success']:.2f} J", self.COLORS["filter_online"])
        self._make_card(row2, "Avg Energy / Failed", f"{global_data['avg_failed']:.2f} J", self.COLORS["filter_offline"])

    def _render_table(self, tree, data_dict):
        """Populates a Treeview with the provided dictionary data."""
        # Clear existing items
        for item in tree.get_children():
            tree.delete(item)
            
        count = 0
        for key, m in data_dict.items():
            # Decide if the row is even or odd for the zebra stripe effect
            tag = 'evenrow' if count % 2 == 0 else 'oddrow'
            
            tree.insert("", tk.END, values=(
                key,
                f"{m['total_energy']:.2f} J",
                f"{m['used_energy']:.2f} J",
                f"{m['wasted_energy']:.2f} J",
                f"{m['avg_per_print']:.2f} J",
                f"{m['avg_success']:.2f} J",
                f"{m['avg_failed']:.2f} J"
            ), tags=(tag,))
            
            count += 1
        