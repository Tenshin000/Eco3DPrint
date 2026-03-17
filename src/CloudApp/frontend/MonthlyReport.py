import json
import tkinter as tk

from tkinter import ttk
from utility.Log import Log

class MonthlyReportScreen:
    """Module handling the Monthly Energy Report interface and logic."""
    def __init__(self, parent_frame, send_ws_callback, colors):
        self.parent = parent_frame
        self.send_ws_message = send_ws_callback
        self.COLORS = colors

        self.logger = Log(logger_name="monthly_report", module_name="REPORT").get_logger()
        self.logger.info("Monthly Report Screen initialized.")

        self.J_TO_KWH = 3600000.0  # Conversion factor: 1 kWh = 3.6 * 10^6 Joules

        self._build_ui()

    def _build_ui(self):
        # TOP CONTROL BAR
        control_frame = tk.Frame(self.parent, bg=self.COLORS["bg"])
        control_frame.pack(fill=tk.X, padx=20, pady=10)

        refresh_btn = tk.Button(
            control_frame, text="Refresh Monthly Report", font=("Helvetica", 12, "bold"),
            bg=self.COLORS["button_bg"], fg=self.COLORS["text_light"], relief=tk.FLAT,
            command=self.request_report
        )
        refresh_btn.pack(side=tk.RIGHT, ipady=5, ipadx=10)

        title_lbl = tk.Label(
            control_frame, text="Monthly Energy Consumption (Current Year)", 
            bg=self.COLORS["bg"], fg=self.COLORS["text_light"], font=("Helvetica", 16, "bold")
        )
        title_lbl.pack(side=tk.LEFT)

        # YEAR-TO-DATE SUMMARY CARDS
        self.summary_frame = tk.Frame(self.parent, bg=self.COLORS["card_bg"])
        self.summary_frame.pack(fill=tk.X, padx=20, pady=(0, 10))
        
        # We will populate these labels dynamically
        self.lbl_tot_prints = self._make_summary_card("Total Prints (YTD)", "#E8F5E9")
        self.lbl_tot_energy = self._make_summary_card("Total Energy (YTD)", "#00A2FF")
        self.lbl_used_energy = self._make_summary_card("Well-Used (YTD)", self.COLORS["filter_online"])
        self.lbl_wasted_energy = self._make_summary_card("Wasted (YTD)", self.COLORS["filter_offline"])

        # MONTHLY TABLE (TREEVIEW)
        table_frame = tk.Frame(self.parent, bg=self.COLORS["bg"])
        table_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        style = ttk.Style()
        style.theme_use('default')
        style.configure("Treeview", 
                        background=self.COLORS["card_bg"],
                        foreground=self.COLORS["text_light"],
                        fieldbackground=self.COLORS["card_bg"],
                        rowheight=35,
                        font=('Helvetica', 11),
                        borderwidth=0)
        style.map("Treeview", background=[('selected', self.COLORS["button_bg"])])
        style.configure("Treeview.Heading", 
                        background=self.COLORS["header"], 
                        foreground=self.COLORS["text_light"], 
                        font=('Helvetica', 12, 'bold'),
                        borderwidth=1,
                        relief="flat")

        columns = ("month", "prints", "total", "used", "wasted")
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings", height=12)
        
        self.tree.heading("month", text="Month")
        self.tree.heading("prints", text="Total Prints")
        self.tree.heading("total", text="Total Energy (kWh)")
        self.tree.heading("used", text="Well-Used (kWh)")
        self.tree.heading("wasted", text="Wasted (kWh)")
        
        self.tree.column("month", anchor=tk.W, width=150)
        for col in columns[1:]:
            self.tree.column(col, anchor=tk.CENTER, width=150)

        self.tree.tag_configure('oddrow', background=self.COLORS["bg"])
        self.tree.tag_configure('evenrow', background=self.COLORS["card_bg"])

        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def _make_summary_card(self, title, value_color):
        """Creates a single summary card and returns the Label widget for the value."""
        card = tk.Frame(self.summary_frame, bg=self.COLORS["bg"], bd=1, relief=tk.RIDGE)
        card.pack(side=tk.LEFT, padx=10, pady=10, fill=tk.BOTH, expand=True)
        tk.Label(card, text=title, bg=self.COLORS["bg"], fg=self.COLORS["text_muted"], font=("Helvetica", 11, "bold")).pack(pady=(10, 0))
        val_label = tk.Label(card, text="--", bg=self.COLORS["bg"], fg=value_color, font=("Helvetica", 18, "bold"))
        val_label.pack(pady=(5, 10))
        return val_label

    def request_report(self):
        self.logger.info("Requesting Monthly Report from backend...")
        self.send_ws_message({"action": "get_monthly_report"})

    def process_incoming_data(self, json_payload):
        try:
            parsed_data = json.loads(json_payload)
            monthly_data = parsed_data.get("data", {})
            
            # Variables to calculate Year-To-Date summary
            ytd_prints = 0
            ytd_total_kwh = 0.0
            ytd_used_kwh = 0.0
            ytd_wasted_kwh = 0.0

            # Clear existing table data
            for item in self.tree.get_children():
                self.tree.delete(item)
                
            count = 0
            for month_name, m_data in monthly_data.items():
                display_month = str(month_name).upper()

                # Extract Joules and convert to kWh
                prints = m_data.get("prints_count", 0)
                tot_kwh = m_data.get("total_energy", 0.0) / self.J_TO_KWH
                used_kwh = m_data.get("used_energy", 0.0) / self.J_TO_KWH
                wasted_kwh = m_data.get("wasted_energy", 0.0) / self.J_TO_KWH

                # Accumulate for YTD
                ytd_prints += prints
                ytd_total_kwh += tot_kwh
                ytd_used_kwh += used_kwh
                ytd_wasted_kwh += wasted_kwh

                # Insert into table
                tag = 'evenrow' if count % 2 == 0 else 'oddrow'
                self.tree.insert("", tk.END, values=(
                    display_month,
                    prints,
                    f"{tot_kwh:.4f}",
                    f"{used_kwh:.4f}",
                    f"{wasted_kwh:.4f}"
                ), tags=(tag,))
                count += 1
            
            # Update Summary Cards
            self.lbl_tot_prints.config(text=str(ytd_prints))
            self.lbl_tot_energy.config(text=f"{ytd_total_kwh:.3f} kWh")
            self.lbl_used_energy.config(text=f"{ytd_used_kwh:.3f} kWh")
            self.lbl_wasted_energy.config(text=f"{ytd_wasted_kwh:.3f} kWh")

            self.logger.info("Monthly Report rendered successfully.")
            
        except Exception as e:
            self.logger.error(f"Error processing monthly report data: {e}")