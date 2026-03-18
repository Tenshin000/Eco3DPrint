import json
import tkinter as tk

from tkinter import ttk
from utility.Log import Log

class WeeklyReportScreen:
    """Module handling the Weekly Energy Report interface and logic."""
    def __init__(self, parent_frame, send_ws_callback, colors):
        self.parent = parent_frame
        self.send_ws_message = send_ws_callback
        self.COLORS = colors

        self.logger = Log(logger_name="weekly_report", module_name="REPORT").get_logger()
        self.logger.info("Weekly Report Screen initialized.")

        # Conversion factor: 1 kWh = 3.6 * 10^6 Joules
        self.J_TO_KWH = 3600000.0

        self._build_ui()

    def _build_ui(self):
        """Constructs the Weekly Report interface."""
        # Top Bar for controls
        control_frame = tk.Frame(self.parent, bg=self.COLORS["bg"])
        control_frame.pack(fill=tk.X, padx=20, pady=10)

        refresh_btn = tk.Button(
            control_frame, text="Refresh Weekly Report", font=("Helvetica", 12, "bold"),
            bg=self.COLORS["button_bg"], fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self.request_report
        )
        refresh_btn.pack(side=tk.RIGHT, ipady=5, ipadx=10)

        title_lbl = tk.Label(
            control_frame, text="Weekly Energy Consumption Analysis", 
            bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 16, "bold")
        )
        title_lbl.pack(side=tk.LEFT)

        # Internal Notebook and Treeview Styles
        style = ttk.Style()
        style.theme_use('default')
        
        style.configure('Inner.TNotebook', background=self.COLORS["bg"], borderwidth=0)
        style.configure('Inner.TNotebook.Tab', background=self.COLORS["card_bg"], foreground=self.COLORS["text_light"], padding=[10, 2])
        style.map('Inner.TNotebook.Tab', background=[('selected', self.COLORS["header"])])

        self.inner_notebook = ttk.Notebook(self.parent, style='Inner.TNotebook')
        self.inner_notebook.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        # Tab 1: Global
        self.global_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["bg"])
        self.inner_notebook.add(self.global_frame, text=" Global Summary ")
        
        self.global_cards_frame = tk.Frame(self.global_frame, bg=self.COLORS["bg"])
        self.global_cards_frame.pack(fill=tk.X, padx=10, pady=10)
        
        self.global_table_frame = tk.Frame(self.global_frame, bg=self.COLORS["bg"])
        self.global_table_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        # For the global table, we center it by wrapping it inside a centered frame
        self.global_table_center = tk.Frame(self.global_table_frame, bg=self.COLORS["bg"])
        self.global_table_center.pack(anchor=tk.CENTER, pady=10)
        self.global_tree = self._create_treeview(self.global_table_center, "Day", height=7)

        # Tab 2: By Printer
        self.printer_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["bg"])
        self.inner_notebook.add(self.printer_frame, text=" By Printer ")
        self.printer_trees = self._create_scrollable_daily_tables(self.printer_frame, "Printer IP")

        # Tab 3: By STL
        self.stl_frame = tk.Frame(self.inner_notebook, bg=self.COLORS["bg"])
        self.inner_notebook.add(self.stl_frame, text=" By STL ")
        self.stl_trees = self._create_scrollable_daily_tables(self.stl_frame, "STL Name")

        # Initial prompt inside the cards frame
        tk.Label(
            self.global_cards_frame, 
            text="Fetching weekly data...", 
            bg=self.COLORS["bg"], 
            fg=self.COLORS["text_muted"], 
            font=("Helvetica", 14)
        ).pack(pady=40)

    def _create_scrollable_daily_tables(self, parent, first_col_name):
        """Creates a scrollable canvas containing 7 centered smaller Treeviews."""
        canvas = tk.Canvas(parent, bg=self.COLORS["bg"], highlightthickness=0)
        scrollbar = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg=self.COLORS["bg"])

        # Create window inside canvas
        window_id = canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")

        # Configure canvas to resize the inner frame to match its width (for centering)
        def on_canvas_configure(event):
            canvas.itemconfig(window_id, width=event.width)
            canvas.configure(scrollregion=canvas.bbox("all"))

        canvas.bind("<Configure>", on_canvas_configure)
        scrollable_frame.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
        trees_dict = {}

        for day in days:
            # Day title centered
            lbl = tk.Label(scrollable_frame, text=day, bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 14, "bold"))
            lbl.pack(anchor=tk.CENTER, pady=(25, 5))
            
            # Container for the daily treeview packed with anchor=CENTER
            tree_container = tk.Frame(scrollable_frame, bg=self.COLORS["bg"])
            tree_container.pack(anchor=tk.CENTER, pady=5)
            
            tree = self._create_treeview(tree_container, first_col_name, height=4)
            trees_dict[day] = tree

        return trees_dict

    def _create_treeview(self, parent, first_col_name, height=15):
        """Creates a standardized Treeview for tabular data display."""
        columns = ("name", "total", "used", "wasted", "avg_print", "avg_succ", "avg_fail")
        tree = ttk.Treeview(parent, columns=columns, show="headings", height=height)
        
        tree.heading("name", text=first_col_name)
        tree.heading("total", text="Total Energy")
        tree.heading("used", text="Well-Used (Finished)")
        tree.heading("wasted", text="Wasted (Failed/Error)")
        tree.heading("avg_print", text="Avg / Print")
        tree.heading("avg_succ", text="Avg / Success")
        tree.heading("avg_fail", text="Avg / Failed")
        
        for col in columns:
            tree.column(col, anchor=tk.CENTER, width=130) # Slightly widened to ensure columns display headers nicely
        tree.column("name", width=200, anchor=tk.W)

        tree.tag_configure('oddrow', background=self.COLORS["bg"])
        tree.tag_configure('evenrow', background=self.COLORS["card_bg"])

        scrollbar = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=tree.yview)
        tree.configure(yscroll=scrollbar.set)
        
        tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        return tree

    def request_report(self):
        """Sends a WebSocket request to generate the daily report."""
        self.logger.info("Requesting Weekly Report from backend...")
        self.send_ws_message({"action": "get_weekly_report"})

    def process_incoming_data(self, json_payload):
        """Parses and renders the daily report data into the UI, padding missing days with zeros."""
        try:
            parsed_data = json.loads(json_payload)
            report = parsed_data.get("data", {})
            
            printer_by_day = report.get("printer_by_day", {})
            stl_by_day = report.get("stl_by_day", {})

            # 1. Identify all unique printers and STLs across the entire week
            all_printers = set()
            for day_data in printer_by_day.values():
                all_printers.update(day_data.keys())

            all_stls = set()
            for day_data in stl_by_day.values():
                all_stls.update(day_data.keys())

            days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
            
            # 2. Template for a 0-filled record
            empty_metrics = {
                "total_energy": 0.0,
                "used_energy": 0.0,
                "wasted_energy": 0.0,
                "avg_per_print": 0.0,
                "avg_success": 0.0,
                "avg_failed": 0.0
            }

            # 3. Inject zeros where data is missing
            for day in days:
                if day not in printer_by_day:
                    printer_by_day[day] = {}
                if day not in stl_by_day:
                    stl_by_day[day] = {}

                for p in all_printers:
                    if p not in printer_by_day[day]:
                        printer_by_day[day][p] = empty_metrics

                for s in all_stls:
                    if s not in stl_by_day[day]:
                        stl_by_day[day][s] = empty_metrics

            # Render Global Summary and Global Daily Table
            self._render_global(report.get("global_summary", {}), report.get("global_by_day", {}))
            
            # Render 7 daily tables for Printers (now fully padded with zeros)
            self._render_multiple_tables(self.printer_trees, printer_by_day)
            
            # Render 7 daily tables for STLs (now fully padded with zeros)
            self._render_multiple_tables(self.stl_trees, stl_by_day)
            
            self.logger.info("Weekly Report rendered successfully with zero-padded rows.")
            
        except Exception as e:
            self.logger.error(f"Error processing weekly report data: {e}")

    def _make_card(self, parent, title, value, color):
        "Make the cards for the UI"
        frame = tk.Frame(parent, bg=self.COLORS["bg"], bd=1, relief=tk.RIDGE)
        frame.pack(side=tk.LEFT, padx=10, pady=5, fill=tk.BOTH, expand=True)
        tk.Label(frame, text=title, bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], font=("Helvetica", 11, "bold")).pack(pady=(15, 0))
        tk.Label(frame, text=value, bg=self.COLORS["bg"], fg=color, font=("Helvetica", 18, "bold")).pack(pady=(5, 15))

    def _render_global(self, global_summary, global_daily):
        """Draws the Global Summary dashboard cards and the global daily table."""
        # Clear existing content in cards frame
        for widget in self.global_cards_frame.winfo_children():
            widget.destroy()

        if not global_summary or global_summary.get("prints_count", 0) == 0:
            tk.Label(
                self.global_cards_frame, 
                text="No prints recorded this week.", 
                bg=self.COLORS["bg"], 
                fg=self.COLORS["text_muted"], 
                font=("Helvetica", 14)
            ).pack(pady=40)
            
            # Clear global table too
            for item in self.global_tree.get_children():
                self.global_tree.delete(item)
            return

        # Top Section: Total Prints Centered
        top_row = tk.Frame(self.global_cards_frame, bg=self.COLORS["bg"])
        top_row.pack(fill=tk.X, pady=5)
        
        prints_frame = tk.Frame(top_row, bg=self.COLORS["bg"], bd=1, relief=tk.RIDGE)
        prints_frame.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)
        tk.Label(prints_frame, text="Total Prints This Week", bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], font=("Helvetica", 12, "bold")).pack(pady=(15, 0))
        tk.Label(prints_frame, text=str(global_summary["prints_count"]), bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 24, "bold")).pack(pady=(5, 15))

        # Convert Joules to kWh and display with 4 decimals
        row1 = tk.Frame(self.global_cards_frame, bg=self.COLORS["bg"])
        row1.pack(fill=tk.X, pady=5)
        self._make_card(row1, "Total Energy", f"{global_summary['total_energy'] / self.J_TO_KWH:.4f} kWh", "#00A2FF")
        self._make_card(row1, "Well-Used Energy", f"{global_summary['used_energy'] / self.J_TO_KWH:.4f} kWh", self.COLORS["filter_online"])
        self._make_card(row1, "Wasted Energy", f"{global_summary['wasted_energy'] / self.J_TO_KWH:.4f} kWh", self.COLORS["filter_offline"])

        # Bottom Section: Averages
        row2 = tk.Frame(self.global_cards_frame, bg=self.COLORS["bg"])
        row2.pack(fill=tk.X, pady=5)
        self._make_card(row2, "Avg Energy / Print", f"{global_summary['avg_per_print'] / self.J_TO_KWH:.4f} kWh", "#00A2FF")
        self._make_card(row2, "Avg Energy / Success", f"{global_summary['avg_success'] / self.J_TO_KWH:.4f} kWh", self.COLORS["filter_online"])
        self._make_card(row2, "Avg Energy / Failed", f"{global_summary['avg_failed'] / self.J_TO_KWH:.4f} kWh", self.COLORS["filter_offline"])

        # Populate Global Daily Table
        self._populate_single_table(self.global_tree, global_daily)

    def _render_multiple_tables(self, trees_dict, grouped_data):
        """Iterates through the 7 daily trees and populates them with data."""
        for day, tree in trees_dict.items():
            daily_data = grouped_data.get(day, {})
            self._populate_single_table(tree, daily_data)

    def _populate_single_table(self, tree, data_dict):
        """Populates a single Treeview with the provided dictionary data."""
        # Clear existing items
        for item in tree.get_children():
            tree.delete(item)
            
        count = 0
        # Sort keys to ensure consistent order (e.g. Printer IPs are ordered the same every day)
        for key in sorted(data_dict.keys()):
            m = data_dict[key]
            tag = 'evenrow' if count % 2 == 0 else 'oddrow'
            
            tree.insert("", tk.END, values=(
                key,
                f"{m['total_energy'] / self.J_TO_KWH:.4f} kWh",
                f"{m['used_energy'] / self.J_TO_KWH:.4f} kWh",
                f"{m['wasted_energy'] / self.J_TO_KWH:.4f} kWh",
                f"{m['avg_per_print'] / self.J_TO_KWH:.4f} kWh",
                f"{m['avg_success'] / self.J_TO_KWH:.4f} kWh",
                f"{m['avg_failed'] / self.J_TO_KWH:.4f} kWh"
            ), tags=(tag,))
            count += 1