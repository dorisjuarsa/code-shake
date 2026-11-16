#!/usr/bin/env python3
"""
=============================================================================
COCA-COLA BOTTLE TEST BENCH & BATTERY PROFILER
=============================================================================
- Compact, Minimalist & Focused for Hardware Testing & Battery Profiling
- Single Unified Auto-Detect Engine (Direct BlueZ Native BLE via Bleak)
- Live RSSI (Signal Strength in dBm) Monitoring
- Live Battery Discharge Graph (Real-time PyQt6 Canvas)
- One-Click Session CSV Logger for Battery Life Analysis
- Animated Compact Coca-Cola Bottle with Soda Fill & Fizz Bubbles
=============================================================================
"""

import sys
import os
import asyncio
import time
import math
import random
import struct
import wave
import atexit
import signal
import csv
import re
import subprocess

from PyQt6.QtCore import (
    Qt, QThread, pyqtSignal, pyqtSlot, QTimer, QRectF, QPointF, QUrl
)
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QProgressBar, QFrame, QScrollArea,
    QGridLayout, QSizePolicy
)
from PyQt6.QtGui import (
    QPainter, QPainterPath, QColor, QLinearGradient,
    QPen, QFont, QPolygonF
)
from PyQt6.QtMultimedia import QSoundEffect

import bleak
from bleak import BleakScanner, BleakClient

# =============================================================================
# CONSTANTS & CONFIGURATION
# =============================================================================
SERVICE_UUID = "19b10000-e8f2-537e-4f6c-d104768a1214"
CHAR_UUID    = "19b10001-e8f2-537e-4f6c-d104768a1214"

BOTTLE_REGISTRY = {
    "COCA-COLA-BOTTLE-01": {
        "label": "BOTOL 01",
        "mac": "44:BD:8D:77:5D:81",
        "color": "#F40009"
    },
    "COCA-COLA-BOTTLE-02": {
        "label": "BOTOL 02",
        "mac": "44:BD:8D:77:56:25",
        "color": "#00B0FF"
    }
}

APP_DIR = os.path.dirname(os.path.abspath(__file__))
SOUND_DIR = os.path.join(APP_DIR, "sounds")
LOGS_DIR = os.path.join(APP_DIR, "logs")
os.makedirs(LOGS_DIR, exist_ok=True)

# =============================================================================
# ZERO-DEPENDENCY SOUND MANAGER
# =============================================================================
def ensure_sound_files():
    os.makedirs(SOUND_DIR, exist_ok=True)
    fizz_path = os.path.join(SOUND_DIR, "fizz.wav")
    connect_path = os.path.join(SOUND_DIR, "connect.wav")
    disconnect_path = os.path.join(SOUND_DIR, "disconnect.wav")

    rate = 44100
    if not os.path.exists(fizz_path):
        duration = 0.12
        n = int(rate * duration)
        with wave.open(fizz_path, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            last = 0
            for i in range(n):
                t = i / n
                env = math.exp(-t * 14)
                r = random.uniform(-1, 1)
                filtered = 0.65 * (r - last) + 0.35 * r
                last = r
                val = int(filtered * env * 14000)
                frames.extend(struct.pack('<h', max(-32768, min(32767, val))))
            w.writeframes(frames)

    if not os.path.exists(connect_path):
        with wave.open(connect_path, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            for f in [440.0, 554.37, 659.25]:
                n = int(rate * 0.07)
                for i in range(n):
                    t = i / rate
                    env = math.sin(math.pi * (i / n))
                    val = int(math.sin(2 * math.pi * f * t) * env * 12000)
                    frames.extend(struct.pack('<h', max(-32768, min(32767, val))))
            w.writeframes(frames)

    if not os.path.exists(disconnect_path):
        with wave.open(disconnect_path, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            frames = bytearray()
            n = int(rate * 0.18)
            for i in range(n):
                t = i / rate
                env = math.exp(-i / (rate * 0.1))
                val = int(math.sin(2 * math.pi * 220.0 * t) * env * 10000)
                frames.extend(struct.pack('<h', max(-32768, min(32767, val))))
            w.writeframes(frames)

class SoundManager:
    def __init__(self):
        ensure_sound_files()
        self.muted = False
        self.last_fizz_time = 0

        self.fizz_sound = QSoundEffect()
        self.fizz_sound.setSource(QUrl.fromLocalFile(os.path.join(SOUND_DIR, "fizz.wav")))
        self.fizz_sound.setVolume(0.70)

        self.connect_sound = QSoundEffect()
        self.connect_sound.setSource(QUrl.fromLocalFile(os.path.join(SOUND_DIR, "connect.wav")))
        self.connect_sound.setVolume(0.80)

        self.disconnect_sound = QSoundEffect()
        self.disconnect_sound.setSource(QUrl.fromLocalFile(os.path.join(SOUND_DIR, "disconnect.wav")))
        self.disconnect_sound.setVolume(0.60)

    def play_fizz(self):
        if self.muted: return
        now = time.time()
        if now - self.last_fizz_time >= 0.05:
            self.last_fizz_time = now
            self.fizz_sound.play()

    def play_connect(self):
        if not self.muted: self.connect_sound.play()

    def play_disconnect(self):
        if not self.muted: self.disconnect_sound.play()

# =============================================================================
# LIVE BATTERY DISCHARGE & SHAKE INTENSITY GRAPH WIDGET (PyQt6 QPainter)
# =============================================================================
class BatteryGraphWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(320, 240)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        # Separate series for Battery (%) and Shake Intensity (0-100)
        self.battery_points = [] # list of (elapsed_sec, batt_pct)
        self.shake_points = []   # list of (elapsed_sec, intensity)
        self.duration_sec = 0
        self.initial_batt = None
        self.current_batt = None
        self.total_shakes = 0

    def add_point(self, elapsed_sec, batt_pct, intensity=0):
        if batt_pct >= 0:
            if self.initial_batt is None:
                self.initial_batt = batt_pct
            self.current_batt = batt_pct
            self.battery_points.append((float(elapsed_sec), float(batt_pct)))

        if intensity > 0:
            self.total_shakes += 1
            self.shake_points.append((float(elapsed_sec), float(intensity)))

        self.duration_sec = max(self.duration_sec, elapsed_sec)
        self.update()

    def reset_graph(self):
        self.battery_points = []
        self.shake_points = []
        self.duration_sec = 0
        self.initial_batt = None
        self.current_batt = None
        self.total_shakes = 0
        self.update()

    def _build_smooth_path(self, pts, y_min=None, y_max=None):
        """Menghasilkan QPainterPath kurva halus melengkung (Catmull-Rom Bezier Spline) tanpa sudut tajam."""
        if not pts:
            return QPainterPath()
        path = QPainterPath()
        path.moveTo(pts[0])
        if len(pts) == 1:
            return path
        if len(pts) == 2:
            path.lineTo(pts[1])
            return path

        n = len(pts)
        for i in range(n - 1):
            p0 = pts[i - 1] if i > 0 else QPointF(2 * pts[0].x() - pts[1].x(), 2 * pts[0].y() - pts[1].y())
            p1 = pts[i]
            p2 = pts[i + 1]
            p3 = pts[i + 2] if i + 2 < n else QPointF(2 * pts[-1].x() - pts[-2].x(), 2 * pts[-1].y() - pts[-2].y())

            c1 = QPointF(p1.x() + (p2.x() - p0.x()) / 6.0, p1.y() + (p2.y() - p0.y()) / 6.0)
            c2 = QPointF(p2.x() - (p3.x() - p1.x()) / 6.0, p2.y() - (p3.y() - p1.y()) / 6.0)

            if y_min is not None and y_max is not None:
                c1.setY(max(y_min, min(y_max, c1.y())))
                c2.setY(max(y_min, min(y_max, c2.y())))

            path.cubicTo(c1, c2, p2)
        return path

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        w = self.width()
        h = self.height()

        # Background Card
        bg_rect = QRectF(0, 0, w, h)
        painter.setBrush(QColor("#111115"))
        painter.setPen(QPen(QColor("#24242D"), 1.2))
        painter.drawRoundedRect(bg_rect, 10, 10)

        # Plot margins
        pad_l = 44.0
        pad_r = 16.0
        pad_t = 34.0
        pad_b = 26.0

        plot_w = max(10.0, w - pad_l - pad_r)
        plot_h = max(10.0, h - pad_t - pad_b)

        # Header Title & Legend
        painter.setFont(QFont("Segoe UI", 9, QFont.Weight.Bold))
        painter.setPen(QColor("#00E5FF"))
        painter.drawText(int(pad_l), int(pad_t - 14), "📈 TELEMETRI:")

        # Legend items
        legend_x = pad_l + 84
        painter.setFont(QFont("Segoe UI", 8, QFont.Weight.Bold))
        painter.setPen(QColor("#00E676"))
        painter.drawText(int(legend_x), int(pad_t - 14), "🟢 Baterai (%)")
        painter.setPen(QColor("#FFD600"))
        painter.drawText(int(legend_x + 90), int(pad_t - 14), "🟡 Sentakan (0-100)")

        # Stats on right side of header
        if self.initial_batt is not None and self.current_batt is not None:
            delta = self.current_batt - self.initial_batt
            sign = "+" if delta > 0 else ""
            m, s = divmod(int(self.duration_sec), 60)
            stats_txt = f"{m:02d}:{s:02d} | Kocok: {self.total_shakes} | Δ: {sign}{delta:.0f}%"
            painter.setFont(QFont("Segoe UI", 8))
            painter.setPen(QColor("#A0A0B0"))
            painter.drawText(QRectF(pad_l, pad_t - 24, plot_w, 18), Qt.AlignmentFlag.AlignRight, stats_txt)

        # Grid lines & Y-Axis Labels (0, 20, 40, 60, 80, 100)
        painter.setFont(QFont("Segoe UI", 8))
        pen_grid = QPen(QColor("#202028"), 1.0, Qt.PenStyle.DashLine)
        pen_zero = QPen(QColor("#353545"), 1.0, Qt.PenStyle.SolidLine)

        for pct in [0, 20, 40, 60, 80, 100]:
            y = pad_t + plot_h - (pct / 100.0) * plot_h
            painter.setPen(pen_zero if pct == 0 else pen_grid)
            painter.drawLine(int(pad_l), int(y), int(pad_l + plot_w), int(y))

            painter.setPen(QColor("#707080"))
            painter.drawText(QRectF(0, y - 7, pad_l - 6, 14), Qt.AlignmentFlag.AlignRight, f"{pct}")

        # Time X-Axis range (auto-expand: min 60s, then 120, 300, etc.)
        max_t = max(60.0, self.duration_sec)
        num_t_divs = 4
        pen_t_grid = QPen(QColor("#181820"), 1.0, Qt.PenStyle.DotLine)

        for i in range(num_t_divs + 1):
            t_val = (max_t / num_t_divs) * i
            x = pad_l + (t_val / max_t) * plot_w
            if i > 0 and i < num_t_divs:
                painter.setPen(pen_t_grid)
                painter.drawLine(int(x), int(pad_t), int(x), int(pad_t + plot_h))

            m, s = divmod(int(t_val), 60)
            t_str = f"{m:02d}:{s:02d}"
            painter.setPen(QColor("#606070"))
            painter.drawText(QRectF(x - 20, pad_t + plot_h + 6, 40, 14), Qt.AlignmentFlag.AlignCenter, t_str)

        # If no points, show friendly placeholder
        if not self.battery_points and not self.shake_points:
            painter.setFont(QFont("Segoe UI", 9, QFont.Weight.Medium))
            painter.setPen(QColor("#505060"))
            painter.drawText(QRectF(pad_l, pad_t, plot_w, plot_h), Qt.AlignmentFlag.AlignCenter,
                             "Menunggu data... Goncang botol atau klik [Mulai Rekam Sesi]")
            return

        # -------------------------------------------------------------
        # 1. DRAW SHAKE INTENSITY (SMOOTH CURVED SPLINE, ZERO DOTS)
        # -------------------------------------------------------------
        if self.shake_points:
            shake_line_pts = []
            for t_val, i_val in self.shake_points:
                x = pad_l + (t_val / max_t) * plot_w
                y = pad_t + plot_h - (i_val / 100.0) * plot_h
                shake_line_pts.append(QPointF(x, y))

            # Kurva gelombang halus (Catmull-Rom cubic Bezier) tanpa dot sama sekali
            if len(shake_line_pts) >= 2:
                shake_path = self._build_smooth_path(shake_line_pts, pad_t, pad_t + plot_h)
                pen_shake = QPen(QColor(255, 214, 0, 130), 1.1, Qt.PenStyle.SolidLine)
                pen_shake.setCapStyle(Qt.PenCapStyle.RoundCap)
                pen_shake.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
                painter.setPen(pen_shake)
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.strokePath(shake_path, pen_shake)

        # -------------------------------------------------------------
        # 2. DRAW BATTERY DISCHARGE (SMOOTH GREEN CURVE & AREA GRADIENT)
        # -------------------------------------------------------------
        if self.battery_points:
            line_points = []
            for t_val, b_val in self.battery_points:
                x = pad_l + (t_val / max_t) * plot_w
                y = pad_t + plot_h - (b_val / 100.0) * plot_h
                line_points.append(QPointF(x, y))

            last_pt = line_points[-1]

            # Build smooth curve path
            batt_path = self._build_smooth_path(line_points, pad_t, pad_t + plot_h)

            # Gradient Area Fill under smooth curve
            batt_area = QPainterPath(batt_path)
            batt_area.lineTo(last_pt.x(), pad_t + plot_h)
            batt_area.lineTo(line_points[0].x(), pad_t + plot_h)
            batt_area.closeSubpath()

            grad = QLinearGradient(0, pad_t, 0, pad_t + plot_h)
            grad.setColorAt(0.0, QColor(0, 230, 118, 70))
            grad.setColorAt(0.6, QColor(0, 230, 118, 20))
            grad.setColorAt(1.0, QColor(0, 230, 118, 0))

            painter.setBrush(grad)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.fillPath(batt_area, grad)

            # Solid Smooth Green Line Curve
            pen_line = QPen(QColor("#00E676"), 2.2)
            pen_line.setCapStyle(Qt.PenCapStyle.RoundCap)
            pen_line.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
            painter.setPen(pen_line)
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.strokePath(batt_path, pen_line)

            # Highlight Current / Latest Battery Data Point
            painter.setBrush(QColor("#00E676"))
            painter.setPen(QPen(QColor("#FFFFFF"), 1.8))
            painter.drawEllipse(last_pt, 4.5, 4.5)

            # Floating Current Value Badge
            val_str = f"{int(self.current_batt)}%"
            painter.setFont(QFont("Segoe UI", 9, QFont.Weight.ExtraBold))
            painter.setPen(QColor("#00E676"))
            badge_y = max(pad_t, last_pt.y() - 14)
            painter.drawText(QRectF(last_pt.x() - 25, badge_y - 6, 50, 16),
                             Qt.AlignmentFlag.AlignCenter, val_str)

# =============================================================================
# COMPACT ANIMATED COCA-COLA BOTTLE WIDGET
# =============================================================================
class CompactCokeBottleWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(130, 240)
        self.setMaximumWidth(160)

        self.liquid_level = 0.0 # 0 to 100
        self.target_liquid_level = 0.0

        # Shake wobble animation
        self.shake_offset_x = 0.0
        self.shake_decay_timer = QTimer(self)
        self.shake_decay_timer.timeout.connect(self._on_shake_tick)

        # Bubble simulation
        self.bubbles = []
        for _ in range(10):
            self.bubbles.append({
                'x': random.uniform(35, 75),
                'y': random.uniform(90, 185),
                'r': random.uniform(1.8, 3.2),
                'speed': random.uniform(1.0, 2.2),
                'alpha': random.uniform(0.3, 0.7)
            })

        # Floating score tag (+XX)
        self.float_intensity = 0
        self.float_y = 0.0
        self.float_alpha = 0.0

        # Animation timer (40 FPS)
        self.anim_timer = QTimer(self)
        self.anim_timer.timeout.connect(self._on_anim_tick)
        self.anim_timer.start(25)

    def trigger_shake(self, intensity):
        self.shake_offset_x = random.choice([-6.0, 6.0])
        if not self.shake_decay_timer.isActive():
            self.shake_decay_timer.start(25)

        self.float_intensity = intensity
        self.float_y = 35.0
        self.float_alpha = 1.0

        increment = intensity * 0.14
        self.target_liquid_level = min(100.0, self.target_liquid_level + increment)

    def reset_liquid(self):
        self.liquid_level = 0.0
        self.target_liquid_level = 0.0
        self.float_alpha = 0.0
        self.update()

    def _on_shake_tick(self):
        self.shake_offset_x *= -0.7
        if abs(self.shake_offset_x) < 0.5:
            self.shake_offset_x = 0.0
            self.shake_decay_timer.stop()
        self.update()

    def _on_anim_tick(self):
        if abs(self.liquid_level - self.target_liquid_level) > 0.1:
            self.liquid_level += (self.target_liquid_level - self.liquid_level) * 0.15
        else:
            self.liquid_level = self.target_liquid_level

        liquid_top_y = 195.0 - (self.liquid_level / 100.0) * 175.0
        for b in self.bubbles:
            b['y'] -= b['speed']
            if b['y'] < liquid_top_y:
                b['y'] = random.uniform(180, 195)
                b['x'] = random.uniform(35, 75)

        if self.float_alpha > 0.0:
            self.float_y -= 1.4
            self.float_alpha = max(0.0, self.float_alpha - 0.04)

        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        w = self.width()
        h = self.height()

        scale = min(w / 110.0, h / 215.0) * 0.92
        center_x = w / 2.0 + self.shake_offset_x
        start_y = (h - 215.0 * scale) / 2.0 + 4.0

        painter.save()
        painter.translate(center_x - (55.0 * scale), start_y)
        painter.scale(scale, scale)

        # 1. Compact Contour Silhouette
        coke_path = QPainterPath()
        coke_path.moveTo(46, 20)
        coke_path.lineTo(46, 40)
        coke_path.cubicTo(46, 50, 32, 68, 30, 85)
        coke_path.cubicTo(27, 105, 38, 120, 35, 142)
        coke_path.cubicTo(31, 162, 28, 178, 31, 198)
        coke_path.cubicTo(32, 204, 38, 207, 55, 207)
        coke_path.cubicTo(72, 207, 78, 204, 79, 198)
        coke_path.cubicTo(82, 178, 79, 162, 75, 142)
        coke_path.cubicTo(72, 120, 83, 105, 80, 85)
        coke_path.cubicTo(78, 68, 64, 50, 64, 40)
        coke_path.lineTo(64, 20)
        coke_path.closeSubpath()

        # 2. Draw Glass Body
        painter.fillPath(coke_path, QColor("#121216"))
        painter.strokePath(coke_path, QPen(QColor("#454550"), 1.8))

        # 3. Rising Red Soda Liquid
        painter.save()
        painter.setClipPath(coke_path)

        liquid_y = 207.0 - (self.liquid_level / 100.0) * 187.0
        if self.liquid_level > 0.5:
            coke_grad = QLinearGradient(0, liquid_y, 0, 207)
            coke_grad.setColorAt(0.0, QColor("#FF1744"))
            coke_grad.setColorAt(0.4, QColor("#D50000"))
            coke_grad.setColorAt(1.0, QColor("#350204"))

            liquid_rect = QRectF(0, liquid_y, 110, 210 - liquid_y)
            painter.fillRect(liquid_rect, coke_grad)

            painter.setPen(Qt.PenStyle.NoPen)
            for b in self.bubbles:
                if b['y'] >= liquid_y:
                    b_color = QColor(255, 255, 255, int(b['alpha'] * 255))
                    painter.setBrush(b_color)
                    painter.drawEllipse(QPointF(b['x'], b['y']), b['r'], b['r'])

            painter.setPen(QPen(QColor(255, 255, 255, 160), 1.5))
            painter.drawLine(int(30), int(liquid_y), int(80), int(liquid_y))

        painter.restore()

        # 4. Classic Red Label
        label_path = QPainterPath()
        label_path.moveTo(30, 102)
        label_path.cubicTo(35, 100, 75, 100, 80, 102)
        label_path.lineTo(79, 130)
        label_path.cubicTo(75, 132, 35, 132, 31, 130)
        label_path.closeSubpath()

        painter.fillPath(label_path, QColor("#F40009"))
        painter.strokePath(label_path, QPen(QColor(255, 255, 255, 180), 0.8))

        painter.save()
        painter.setFont(QFont("Segoe UI", 7, QFont.Weight.Bold, italic=True))
        painter.setPen(QColor("#FFFFFF"))
        painter.drawText(QRectF(28, 106, 54, 16), Qt.AlignmentFlag.AlignCenter, "Coca-Cola")
        painter.restore()

        # 5. Cap
        cap_rect = QRectF(43, 11, 24, 10)
        painter.setBrush(QColor("#D50000"))
        painter.setPen(QPen(QColor("#FFFFFF"), 1.0))
        painter.drawRoundedRect(cap_rect, 2, 2)

        # 6. Reflection Shine
        shine_grad = QLinearGradient(0, 0, 110, 0)
        shine_grad.setColorAt(0.0, QColor(255, 255, 255, 60))
        shine_grad.setColorAt(0.3, QColor(255, 255, 255, 15))
        shine_grad.setColorAt(0.7, QColor(0, 0, 0, 30))
        shine_grad.setColorAt(1.0, QColor(255, 255, 255, 50))
        painter.fillPath(coke_path, shine_grad)

        # 7. Floating Intensity Indicator (+XX)
        if self.float_alpha > 0.01:
            painter.save()
            painter.setFont(QFont("Segoe UI", 12, QFont.Weight.ExtraBold))
            color_float = QColor(255, 214, 0, int(self.float_alpha * 255))
            painter.setPen(color_float)
            painter.drawText(QRectF(10, self.float_y, 90, 22), Qt.AlignmentFlag.AlignCenter, f"+{self.float_intensity}")
            painter.restore()

        painter.restore()

# =============================================================================
# SINGLE UNIFIED BLE THREAD (ROBUST & AUTO-RECONNECT)
# =============================================================================
class BleakWorker(QThread):
    sig_status = pyqtSignal(str, str, int, str) # status, bottle_label, rssi, message
    sig_shake = pyqtSignal(str, int, int, int) # bottle_label, pkt_id, intensity, battery
    sig_rssi = pyqtSignal(int) # live rssi
    sig_log = pyqtSignal(str, str) # level, message

    def __init__(self):
        super().__init__()
        self.is_running = True
        self.active_client = None
        self.loop = None
        self.current_mac = None

    def run(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        try:
            self.loop.run_until_complete(self._main_loop())
        finally:
            self.loop.close()

    async def _main_loop(self):
        self.sig_log.emit("INFO", "BLE Direct Engine Aktif. Memindai botol terdekat...")

        while self.is_running:
            discovered_rssi = {"val": -60}

            def match_bottle(device, adv):
                name = (adv.local_name or device.name or "").strip().upper()
                mac = device.address.upper()
                if mac in ["44:BD:8D:77:5D:81", "44:BD:8D:77:56:25"] or "COCA" in name or "BOTTLE" in name:
                    if adv.rssi:
                        discovered_rssi["val"] = adv.rssi
                    return True
                return False

            self.sig_status.emit("SCANNING", "BELUM TERHUBUNG", 0, "Memindai botol... Goncang botol Anda")

            device = None
            try:
                device = await BleakScanner.find_device_by_filter(match_bottle, timeout=3.0)
            except Exception:
                pass

            if not self.is_running:
                break

            if device:
                dev_mac = device.address.upper()
                raw_name = (device.name or "").strip().upper()
                bottle_label = "BOTOL 02" if (dev_mac == "44:BD:8D:77:56:25" or "02" in raw_name) else "BOTOL 01"
                self.current_mac = dev_mac
                rssi = discovered_rssi["val"]

                self.sig_status.emit("CONNECTING", bottle_label, rssi, f"Menyambungkan ke {bottle_label}...")
                self.sig_log.emit("INFO", f"⚡ Menemukan {bottle_label} [{device.address}] ({rssi} dBm). Membuka koneksi...")

                client = BleakClient(device)
                self.active_client = client

                try:
                    await client.connect(timeout=6.0)
                    if client.is_connected:
                        self.sig_status.emit("CONNECTED", bottle_label, rssi, f"Terhubung ke {bottle_label}")
                        self.sig_log.emit("SUCCESS", f"🎉 Berhasil terhubung ke {bottle_label} [{device.address}]!")

                        # 1. Baca initial battery & packet state
                        try:
                            init_bytes = await client.read_gatt_char(CHAR_UUID)
                            init_str = init_bytes.decode("utf-8", errors="ignore").strip()
                            parts = init_str.split(",")
                            if parts[0] == "SHAKE" and len(parts) >= 3:
                                pkt_id = int(parts[1])
                                intensity = int(parts[2])
                                batt = int(parts[3]) if len(parts) >= 4 else -1
                                self.sig_shake.emit(bottle_label, pkt_id, intensity, batt)
                        except Exception:
                            pass

                        # 2. Handler notifikasi streaming
                        def notification_handler(sender, data: bytearray):
                            try:
                                raw = data.decode("utf-8", errors="ignore").strip()
                                parts = raw.split(",")
                                if parts[0] == "SHAKE" and len(parts) >= 3:
                                    pkt_id = int(parts[1])
                                    intensity = int(parts[2])
                                    batt = int(parts[3]) if len(parts) >= 4 else -1
                                    self.sig_shake.emit(bottle_label, pkt_id, intensity, batt)
                            except Exception as err:
                                self.sig_log.emit("WARN", f"Parse error: {err}")

                        await client.start_notify(CHAR_UUID, notification_handler)
                        self.sig_log.emit("INFO", f"Langganan data aktif pada {bottle_label}")

                        # Pertahankan koneksi selama botol aktif & tidak sleep
                        while self.is_running and client.is_connected:
                            await asyncio.sleep(0.25)

                        self.sig_log.emit("WARN", f"{bottle_label} terputus (Deep Sleep / Jarak Jauh). Memindai kembali...")
                    else:
                        self.sig_log.emit("WARN", f"Gagal membuka koneksi ke {bottle_label}")
                except Exception as conn_err:
                    self.sig_log.emit("WARN", f"Koneksi terputus: {conn_err}")
                finally:
                    try:
                        if client.is_connected:
                            await client.disconnect()
                    except Exception:
                        pass
                    self.active_client = None
                    self.current_mac = None
                    self.sig_status.emit("DISCONNECTED", "BELUM TERHUBUNG", 0, "Terputus. Memindai kembali...")

            await asyncio.sleep(0.5)

    def stop(self):
        self.is_running = False
        if self.active_client and self.loop and self.loop.is_running():
            try:
                f = asyncio.run_coroutine_threadsafe(self.active_client.disconnect(), self.loop)
                f.result(timeout=1.2)
            except Exception:
                pass
        self.wait(800)

# =============================================================================
# MAIN WINDOW: COCA-COLA TEST BENCH & BATTERY PROFILER
# =============================================================================
class CokeProfilerApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Coca-Cola Bottle Test Bench & Battery Profiler")
        self.resize(650, 580)
        self.setMinimumSize(600, 520)

        self.sound = SoundManager()

        # Telemetry State
        self.current_bottle = "BELUM TERHUBUNG"
        self.total_shakes = 0
        self.packet_id = 0
        self.intensity = 0
        self.battery = -1
        self.rssi = 0

        # Battery Profiling & Session Logging State
        self.app_start_time = time.time()
        self.is_logging = False
        self.log_file = None
        self.log_writer = None
        self.log_filename = ""
        self.session_start_time = 0
        self.session_samples = 0

        self._init_theme()
        self._build_ui()

        # 1-second Session Profiler Timer
        self.timer_profiler = QTimer(self)
        self.timer_profiler.timeout.connect(self._on_profiler_tick)
        self.timer_profiler.start(1000)

        # Launch BLE Worker
        self.ble_worker = BleakWorker()
        self.ble_worker.sig_status.connect(self._on_ble_status)
        self.ble_worker.sig_shake.connect(self._on_ble_shake)
        self.ble_worker.sig_rssi.connect(self._on_ble_rssi)
        self.ble_worker.sig_log.connect(self._on_ble_log)
        self.ble_worker.start()

    def _init_theme(self):
        self.setStyleSheet("""
            QMainWindow {
                background-color: #0B0B0E;
            }
            QWidget {
                color: #FFFFFF;
                font-family: 'Segoe UI', -apple-system, sans-serif;
            }
            QFrame.card {
                background-color: #141418;
                border: 1px solid #22222A;
                border-radius: 10px;
            }
            QScrollArea {
                border: none;
                background-color: transparent;
            }
        """)

    def _build_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(14, 12, 14, 12)
        main_layout.setSpacing(10)

        # -------------------------------------------------------------
        # 1. TOP HEADER & SOUND TOGGLE
        # -------------------------------------------------------------
        row_head = QHBoxLayout()
        v_title = QVBoxLayout()
        v_title.setSpacing(2)

        lbl_title = QLabel("COCA-COLA BOTTLE TEST BENCH")
        lbl_title.setStyleSheet("color: #F40009; font-size: 19px; font-weight: 900; letter-spacing: 1.2px;")
        lbl_sub = QLabel("Live Telemetry, RSSI Signal & Battery Discharge Profiler")
        lbl_sub.setStyleSheet("color: #7E7E8A; font-size: 11px;")
        v_title.addWidget(lbl_title)
        v_title.addWidget(lbl_sub)
        row_head.addLayout(v_title)

        row_head.addStretch()

        self.btn_mute = QPushButton("🔊 Suara: AKTIF")
        self.btn_mute.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_mute.setStyleSheet("""
            QPushButton {
                background-color: #182230;
                color: #38BDF8;
                font-weight: bold;
                border: 1px solid #0284C7;
                border-radius: 6px;
                padding: 6px 12px;
                font-size: 11px;
            }
            QPushButton:hover { background-color: #1E2D40; }
        """)
        self.btn_mute.clicked.connect(self._toggle_mute)
        row_head.addWidget(self.btn_mute)
        main_layout.addLayout(row_head)

        # -------------------------------------------------------------
        # 2. STATUS CONNECTION & RSSI BAR (COMPACT HIGH-VISIBILITY)
        # -------------------------------------------------------------
        self.status_card = QFrame()
        self.status_card.setStyleSheet("background-color: #261214; border: 1px solid #FF5252; border-radius: 8px; padding: 7px 12px;")
        status_layout = QHBoxLayout(self.status_card)
        status_layout.setContentsMargins(0, 0, 0, 0)

        self.lbl_status_icon = QLabel("📡")
        self.lbl_status_icon.setStyleSheet("font-size: 15px;")
        self.lbl_status_text = QLabel("MEMINDAI BOTOL... (GONCANG BOTOL UNTUK TERHUBUNG)")
        self.lbl_status_text.setStyleSheet("color: #FF5252; font-size: 13px; font-weight: 800;")

        self.lbl_rssi = QLabel("📶 SINYAL: -- dBm")
        self.lbl_rssi.setStyleSheet("color: #888898; font-size: 12px; font-weight: bold;")

        status_layout.addWidget(self.lbl_status_icon)
        status_layout.addWidget(self.lbl_status_text)
        status_layout.addStretch()
        status_layout.addWidget(self.lbl_rssi)
        main_layout.addWidget(self.status_card)

        # -------------------------------------------------------------
        # 3. MINI TELEMETRY CHIPS ROW (4 COMPACT BADGES)
        # -------------------------------------------------------------
        row_chips = QHBoxLayout()
        row_chips.setSpacing(8)

        def make_chip(title, init_val, color):
            box = QFrame()
            box.setStyleSheet("background-color: #15151A; border: 1px solid #24242E; border-radius: 7px; padding: 5px 10px;")
            v = QVBoxLayout(box)
            v.setContentsMargins(0, 0, 0, 0)
            v.setSpacing(1)
            lbl_t = QLabel(title)
            lbl_t.setStyleSheet("color: #727280; font-size: 10px; font-weight: 700; text-transform: uppercase;")
            lbl_v = QLabel(init_val)
            lbl_v.setStyleSheet(f"color: {color}; font-size: 15px; font-weight: 900;")
            v.addWidget(lbl_t)
            v.addWidget(lbl_v)
            return box, lbl_v

        chip_batt, self.lbl_val_batt = make_chip("Baterai", "--%", "#00E676")
        chip_id, self.lbl_val_id     = make_chip("Packet ID", "#0", "#38BDF8")
        chip_int, self.lbl_val_int   = make_chip("Sentakan", "0 / 100", "#FFD600")
        chip_shk, self.lbl_val_shk   = make_chip("Total Kocok", "0", "#FF6D00")

        row_chips.addWidget(chip_batt)
        row_chips.addWidget(chip_id)
        row_chips.addWidget(chip_int)
        row_chips.addWidget(chip_shk)
        main_layout.addLayout(row_chips)

        # -------------------------------------------------------------
        # 4. MAIN VISUAL STAGE (COMPACT BOTTLE ON LEFT, GRAPH ON RIGHT)
        # -------------------------------------------------------------
        row_stage = QHBoxLayout()
        row_stage.setSpacing(10)

        # Left: Compact Bottle
        self.bottle_widget = CompactCokeBottleWidget()
        row_stage.addWidget(self.bottle_widget)

        # Right: Live Battery Discharge Graph
        self.graph_widget = BatteryGraphWidget()
        row_stage.addWidget(self.graph_widget, stretch=1)
        main_layout.addLayout(row_stage)

        # -------------------------------------------------------------
        # 5. BATTERY LOGGING & PROFILING CONTROL BAR
        # -------------------------------------------------------------
        log_ctrl_card = QFrame()
        log_ctrl_card.setStyleSheet("background-color: #121216; border: 1px solid #252530; border-radius: 8px; padding: 7px 12px;")
        v_log_ctrl = QVBoxLayout(log_ctrl_card)
        v_log_ctrl.setContentsMargins(0, 0, 0, 0)
        v_log_ctrl.setSpacing(6)

        h_btn_row = QHBoxLayout()
        h_btn_row.setSpacing(8)

        self.btn_toggle_log = QPushButton("▶ Mulai Rekam Sesi Pengujian")
        self.btn_toggle_log.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_toggle_log.setStyleSheet("""
            QPushButton {
                background-color: #00E676;
                color: #000;
                font-weight: 800;
                font-size: 12px;
                border-radius: 6px;
                padding: 7px 16px;
            }
            QPushButton:hover { background-color: #00C853; }
        """)
        self.btn_toggle_log.clicked.connect(self._toggle_logging_session)
        h_btn_row.addWidget(self.btn_toggle_log)

        self.btn_reset_graph = QPushButton("🔄 Reset Grafik")
        self.btn_reset_graph.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_reset_graph.setStyleSheet("""
            QPushButton {
                background-color: #24242D;
                color: #BBB;
                font-weight: bold;
                font-size: 11px;
                border-radius: 6px;
                border: 1px solid #353545;
                padding: 7px 12px;
            }
            QPushButton:hover { background-color: #2D2D38; color: #FFF; }
        """)
        self.btn_reset_graph.clicked.connect(self._reset_graph_only)
        h_btn_row.addWidget(self.btn_reset_graph)

        h_btn_row.addStretch()

        self.lbl_session_status = QLabel("Status: ⚪ SIAGA (Klik Mulai Rekam)")
        self.lbl_session_status.setStyleSheet("color: #8E8E98; font-size: 11px; font-weight: bold;")
        h_btn_row.addWidget(self.lbl_session_status)
        v_log_ctrl.addLayout(h_btn_row)

        self.lbl_csv_filepath = QLabel("Output: logs/ - (Belum ada sesi aktif)")
        self.lbl_csv_filepath.setStyleSheet("color: #656575; font-size: 10px;")
        v_log_ctrl.addWidget(self.lbl_csv_filepath)

        main_layout.addWidget(log_ctrl_card)

        # -------------------------------------------------------------
        # 6. MINI EVENT STREAM (COMPACT 4-LINE CONSOLE)
        # -------------------------------------------------------------
        self.scroll_log = QScrollArea()
        self.scroll_log.setFixedHeight(75)
        self.scroll_log.setWidgetResizable(True)
        self.log_container = QWidget()
        self.log_vbox = QVBoxLayout(self.log_container)
        self.log_vbox.setContentsMargins(6, 4, 6, 4)
        self.log_vbox.setSpacing(2)
        self.log_vbox.addStretch()
        self.scroll_log.setWidget(self.log_container)
        self.scroll_log.setStyleSheet("background-color: #0E0E12; border-radius: 6px; border: 1px solid #202028;")
        main_layout.addWidget(self.scroll_log)

    # -------------------------------------------------------------
    # SLOTS & EVENT HANDLERS
    # -------------------------------------------------------------
    @pyqtSlot(str, str, int, str)
    def _on_ble_status(self, status, bottle_label, rssi, message):
        self.current_bottle = bottle_label

        if status == "CONNECTED":
            self.status_card.setStyleSheet("background-color: #102414; border: 1px solid #00E676; border-radius: 8px; padding: 7px 12px;")
            self.lbl_status_icon.setText("🟢")
            self.lbl_status_text.setText(f"TERHUBUNG KE: {bottle_label}")
            self.lbl_status_text.setStyleSheet("color: #00E676; font-size: 13px; font-weight: 800;")
            self._update_rssi_display(rssi)
            self.sound.play_connect()
        elif status == "CONNECTING":
            self.status_card.setStyleSheet("background-color: #28200E; border: 1px solid #FFD600; border-radius: 8px; padding: 7px 12px;")
            self.lbl_status_icon.setText("🟡")
            self.lbl_status_text.setText(f"MENYAMBUNGKAN KE {bottle_label}...")
            self.lbl_status_text.setStyleSheet("color: #FFD600; font-size: 13px; font-weight: 800;")
        else: # SCANNING / DISCONNECTED
            self.status_card.setStyleSheet("background-color: #261214; border: 1px solid #FF5252; border-radius: 8px; padding: 7px 12px;")
            self.lbl_status_icon.setText("📡")
            self.lbl_status_text.setText("MEMINDAI BOTOL... (GONCANG BOTOL UNTUK TERHUBUNG)")
            self.lbl_status_text.setStyleSheet("color: #FF5252; font-size: 13px; font-weight: 800;")
            self.lbl_rssi.setText("📶 SINYAL: -- dBm")
            self.lbl_rssi.setStyleSheet("color: #888898; font-size: 12px; font-weight: bold;")
            if status == "DISCONNECTED":
                self.sound.play_disconnect()

    @pyqtSlot(int)
    def _on_ble_rssi(self, rssi):
        self._update_rssi_display(rssi)

    def _update_rssi_display(self, rssi):
        if rssi == 0: return
        self.rssi = rssi

        if rssi >= -60:
            quality = "Sangat Kuat"
            col = "#00E676"
        elif rssi >= -75:
            quality = "Sedang"
            col = "#FFD600"
        else:
            quality = "Lemah"
            col = "#FF5252"

        self.lbl_rssi.setText(f"📶 {rssi} dBm [{quality}]")
        self.lbl_rssi.setStyleSheet(f"color: {col}; font-size: 12px; font-weight: bold;")

    @pyqtSlot(str, int, int, int)
    def _on_ble_shake(self, bottle_label, pkt_id, intensity, batt):
        # Initial status read on connection (pkt_id == 0 and intensity == 0)
        if pkt_id == 0 and intensity == 0:
            if batt >= 0:
                self.battery = batt
                self.lbl_val_batt.setText(f"{batt}%")
                if batt > 50: bcol = "#00E676"
                elif batt >= 20: bcol = "#FFD600"
                else: bcol = "#FF5252"
                self.lbl_val_batt.setStyleSheet(f"color: {bcol}; font-size: 15px; font-weight: 900;")

                # Add to graph
                elapsed = (time.time() - self.session_start_time) if self.is_logging else (time.time() - self.app_start_time)
                self.graph_widget.add_point(elapsed, batt, 0)
                if self.is_logging:
                    self._write_csv_row(elapsed, 0)

            self._log_entry("INFO", f"[{bottle_label}] Data awal terbaca | Baterai: {batt}%")
            return

        self.total_shakes += 1
        self.packet_id = pkt_id
        self.intensity = intensity
        if batt >= 0:
            self.battery = batt

        # Audio & Animation
        self.sound.play_fizz()
        self.bottle_widget.trigger_shake(intensity)

        # Update Mini Badges
        self.lbl_val_shk.setText(str(self.total_shakes))
        self.lbl_val_id.setText(f"#{pkt_id}")
        self.lbl_val_int.setText(f"{intensity} / 100")
        if batt >= 0:
            self.lbl_val_batt.setText(f"{batt}%")
            if batt > 50: bcol = "#00E676"
            elif batt >= 20: bcol = "#FFD600"
            else: bcol = "#FF5252"
            self.lbl_val_batt.setStyleSheet(f"color: {bcol}; font-size: 15px; font-weight: 900;")

        # Feed Graph (Battery curve + Shake intensity spike) & CSV
        elapsed = (time.time() - self.session_start_time) if self.is_logging else (time.time() - self.app_start_time)
        self.graph_widget.add_point(elapsed, self.battery, intensity)
        if self.is_logging:
            self._write_csv_row(elapsed, intensity)

        self._log_entry("SHAKE", f"[{bottle_label}] Shake #{pkt_id} | Sentakan: {intensity} | Baterai: {batt}% | Sinyal: {self.rssi} dBm")

    # -------------------------------------------------------------
    # BATTERY PROFILING & SESSION LOGGING LOGIC
    # -------------------------------------------------------------
    def _toggle_logging_session(self):
        if not self.is_logging:
            # START RECORDING
            self.is_logging = True
            self.session_start_time = time.time()
            self.session_samples = 0
            self.graph_widget.reset_graph()

            stamp = time.strftime("%Y%m%d_%H%M%S")
            self.log_filename = f"battery_test_{stamp}.csv"
            filepath = os.path.join(LOGS_DIR, self.log_filename)

            try:
                self.log_file = open(filepath, "w", newline="", encoding="utf-8")
                self.log_writer = csv.writer(self.log_file)
                self.log_writer.writerow([
                    "Timestamp", "Elapsed_Seconds", "Bottle_Name",
                    "Battery_Percent", "RSSI_dBm", "Packet_ID", "Shake_Intensity"
                ])
                self.log_file.flush()

                self.btn_toggle_log.setText("⏹ Selesai & Simpan CSV")
                self.btn_toggle_log.setStyleSheet("""
                    QPushButton {
                        background-color: #D50000;
                        color: #FFF;
                        font-weight: 800;
                        font-size: 12px;
                        border-radius: 6px;
                        padding: 7px 16px;
                    }
                    QPushButton:hover { background-color: #B71C1C; }
                """)
                self.lbl_session_status.setText("Status: 🔴 MEREKAM (00:00 | 0 Sampel)")
                self.lbl_session_status.setStyleSheet("color: #FF5252; font-size: 11px; font-weight: bold;")
                self.lbl_csv_filepath.setText(f"File: logs/{self.log_filename}")

                self._log_entry("SUCCESS", f"▶ Sesi rekaman baterai DIMULAI: logs/{self.log_filename}")

                # Catat titik awal jika baterai sudah terbaca
                if self.battery >= 0:
                    self.graph_widget.add_point(0, self.battery, 0)
                    self._write_csv_row(0, 0)
            except Exception as e:
                self.is_logging = False
                self._log_entry("ERROR", f"Gagal membuka file CSV: {e}")
        else:
            # STOP RECORDING
            self._stop_logging_session()

    def _stop_logging_session(self):
        if not self.is_logging: return
        self.is_logging = False

        if self.log_file:
            try:
                self.log_file.flush()
                self.log_file.close()
            except Exception:
                pass
            self.log_file = None
            self.log_writer = None

        self.btn_toggle_log.setText("▶ Mulai Rekam Sesi Pengujian")
        self.btn_toggle_log.setStyleSheet("""
            QPushButton {
                background-color: #00E676;
                color: #000;
                font-weight: 800;
                font-size: 12px;
                border-radius: 6px;
                padding: 7px 16px;
            }
            QPushButton:hover { background-color: #00C853; }
        """)
        self.lbl_session_status.setText("Status: ⚪ SESI SELESAI & TERSIMPAN")
        self.lbl_session_status.setStyleSheet("color: #00E676; font-size: 11px; font-weight: bold;")
        self._log_entry("SUCCESS", f"⏹ Rekaman disimpan: logs/{self.log_filename} (Total: {self.session_samples} sampel)")

    def _write_csv_row(self, elapsed_sec, intensity):
        if not self.is_logging or not self.log_writer: return
        now_str = time.strftime("%Y-%m-%d %H:%M:%S")
        batt_val = self.battery if self.battery >= 0 else ""
        rssi_val = self.rssi if self.rssi != 0 else ""

        try:
            self.log_writer.writerow([
                now_str, f"{elapsed_sec:.1f}", self.current_bottle,
                batt_val, rssi_val, self.packet_id, intensity
            ])
            self.log_file.flush()
            self.session_samples += 1
        except Exception:
            pass

    def _on_profiler_tick(self):
        # Update elapsed timer label every second during active logging
        elapsed = (time.time() - self.session_start_time) if self.is_logging else (time.time() - self.app_start_time)

        if self.is_logging:
            m, s = divmod(int(elapsed), 60)
            self.lbl_session_status.setText(f"Status: 🔴 MEREKAM ({m:02d}:{s:02d} | {self.session_samples} Sampel)")

        # Periodic heartbeat (every 2 seconds) to graph battery even when bottle is idle
        if self.battery >= 0 and int(elapsed) % 2 == 0:
            self.graph_widget.add_point(elapsed, self.battery, 0)
            if self.is_logging:
                self._write_csv_row(elapsed, 0)

    def _reset_graph_only(self):
        self.graph_widget.reset_graph()
        self.bottle_widget.reset_liquid()
        self.total_shakes = 0
        self.lbl_val_shk.setText("0")
        self._log_entry("INFO", "🔄 Grafik & hitungan kocokan di-reset.")

    # -------------------------------------------------------------
    # LOG & AUDIO HELPERS
    # -------------------------------------------------------------
    @pyqtSlot(str, str)
    def _on_ble_log(self, level, message):
        self._log_entry(level, message)

    def _log_entry(self, level, message):
        timestamp = time.strftime("%H:%M:%S")
        lbl = QLabel(f"[{timestamp}] {message}")

        if level == "SHAKE":
            lbl.setStyleSheet("color: #00E676; font-size: 10px; font-weight: bold;")
        elif level == "SUCCESS":
            lbl.setStyleSheet("color: #FFD600; font-size: 10px; font-weight: bold;")
        elif level == "WARN":
            lbl.setStyleSheet("color: #FF9100; font-size: 10px;")
        elif level == "ERROR":
            lbl.setStyleSheet("color: #FF5252; font-size: 10px; font-weight: bold;")
        else:
            lbl.setStyleSheet("color: #8E8E98; font-size: 10px;")

        self.log_vbox.addWidget(lbl)

        while self.log_vbox.count() > 80:
            item = self.log_vbox.takeAt(0)
            if item and item.widget():
                item.widget().deleteLater()

        QTimer.singleShot(20, lambda: self.scroll_log.verticalScrollBar().setValue(
            self.scroll_log.verticalScrollBar().maximum()
        ))

    def _toggle_mute(self):
        self.sound.muted = not self.sound.muted
        if self.sound.muted:
            self.btn_mute.setText("🔇 Suara: BISU")
            self.btn_mute.setStyleSheet("""
                QPushButton {
                    background-color: #242428;
                    color: #7E7E88;
                    font-weight: bold;
                    border: 1px solid #383842;
                    border-radius: 6px;
                    padding: 6px 12px;
                    font-size: 11px;
                }
            """)
        else:
            self.btn_mute.setText("🔊 Suara: AKTIF")
            self.btn_mute.setStyleSheet("""
                QPushButton {
                    background-color: #182230;
                    color: #38BDF8;
                    font-weight: bold;
                    border: 1px solid #0284C7;
                    border-radius: 6px;
                    padding: 6px 12px;
                    font-size: 11px;
                }
            """)

    def clean_exit(self):
        self._stop_logging_session()
        self.ble_worker.stop()

    def closeEvent(self, event):
        self.clean_exit()
        event.accept()

# =============================================================================
# CLEANUP HANDLERS (ATEXIT & SIGNAL)
# =============================================================================
def _clean_exit_bluetooth():
    for key, info in BOTTLE_REGISTRY.items():
        try:
            subprocess.run(["bluetoothctl", "disconnect", info["mac"]], capture_output=True, timeout=0.8)
        except Exception:
            pass

atexit.register(_clean_exit_bluetooth)

# =============================================================================
# ENTRYPOINT
# =============================================================================
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Coca-Cola Bottle Test Bench & Battery Profiler")

    window = CokeProfilerApp()
    app.aboutToQuit.connect(window.clean_exit)
    app.aboutToQuit.connect(_clean_exit_bluetooth)

    signal.signal(signal.SIGINT, lambda *args: app.quit())
    sig_timer = QTimer()
    sig_timer.start(250)
    sig_timer.timeout.connect(lambda: None)

    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
