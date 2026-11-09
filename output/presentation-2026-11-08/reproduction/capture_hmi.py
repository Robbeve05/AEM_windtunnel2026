from pathlib import Path
import argparse,asyncio,json,math,os,random,sys,tempfile,time
from datetime import datetime,timezone
from unittest.mock import patch
PACKAGE=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--repo',type=Path,required=True)
args=parser.parse_args(); REPO=args.repo.resolve()
sys.path.insert(0,str(REPO/'HMI'))
# Actual native UI widgets, populated through the real local simulation backend.
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtWidgets import QApplication,QWidget
from PySide6.QtCore import QTimer, QRect
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtGui import QImage,QPainter,QColor,QFontDatabase,QFont
from desktop import HMIWindow
from main import Controller
from backend.aerodynamics.models import ExperimentConfig,AnalysisSettings

app=QApplication.instance() or QApplication([])
# Offscreen Qt does not automatically enumerate system fonts in this runtime.
# Load the same Arial family explicitly; this changes capture setup, not HMI code.
for font in ['arial.ttf','arialbd.ttf','ariali.ttf','arialbi.ttf']:
    font_path=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'/font
    if font_path.is_file():
        assert QFontDatabase.addApplicationFont(str(font_path)) >= 0, font_path
app.setFont(QFont('Arial',10))
random.seed(42)
with tempfile.TemporaryDirectory() as root:
    base=Path(root); (base/'config').mkdir()
    config=json.loads((REPO/'HMI/config/config.json').read_text())
    config['simulation']=True
    (base/'config/config.json').write_text(json.dumps(config),encoding='utf-8')
    controller=Controller(base)
    controller.tunnel.start()
    for k in range(1200):
        controller.sample=controller.tunnel.read(.05)
        controller.sample.update(controller._flow_metrics(controller.sample['airspeed_ms'],time.monotonic()+k*.05))
    controller.sample['host_timestamp']=datetime.now(timezone.utc).isoformat()
    controller.sample['airspeed_kmh']=controller.sample['airspeed_ms']*3.6
    controller.last_valid_sample_monotonic=time.monotonic()

    async def analysis_preview():
        cfg=ExperimentConfig(name='SIM_Wing_AoA5',airfoil='Synthetic diamond section',chord_mm=100,
                             alpha_deg=5,source='simulation',averaging_s=.5,sample_rate_hz=20)
        detail=await controller.aerodynamics.create(cfg); identifier=detail['metadata']['id']
        await controller.aerodynamics.start(identifier)
        await controller.aerodynamics.tasks[identifier]
        await controller.aerodynamics.run_analysis(identifier,AnalysisSettings())
        await controller.aerodynamics.tasks[identifier]
        return controller.aerodynamics.detail(identifier)

    detail=asyncio.run(analysis_preview())
    assert detail['analysis'] is not None,detail['metadata']
    (PACKAGE/'assets/simulated_aerodynamics.json').write_text(json.dumps(detail,indent=2),encoding='utf-8')
    with patch.object(HMIWindow,'_get',lambda *a,**kw:None),patch.object(HMIWindow,'_post',lambda *a,**kw:None):
        w=HMIWindow(); w.resize(1600,900); w.show()
        for timer in w.findChildren(QTimer): timer.stop()
        w._on_config(config,None); w._on_status(controller.status(),None)
        now=time.monotonic()
        w.history=[dict(t=now-60+i*.1,speed=15+.035*math.sin(i*.12),mean=15) for i in range(601)]
        w.chart.set_points(w.history,40)
        app.processEvents()
        w.grab().save(str(PACKAGE/'assets/hmi_dashboard_full.png'))
        # Preserve actual widgets, crop only to keep slide evidence readable.
        rect=w.pages.geometry(); top_left=w.pages.mapTo(w,w.pages.rect().topLeft())
        w.grab(QRect(top_left.x(),top_left.y(),w.pages.width(),min(w.pages.height(),570))).save(str(PACKAGE/'assets/hmi_dashboard.png'))
        w.right.findChild(QWidget,'development').grab().save(str(PACKAGE/'assets/hmi_control.png'))
        w._show_page('cp'); panel=w.aerodynamic_panel
        panel.identifier=detail['metadata']['id']; panel.experiments=[detail['metadata']]
        panel.render(detail); panel.tabs.setCurrentIndex(1)
        app.processEvents()
        w.grab().save(str(PACKAGE/'assets/hmi_analytics_full.png'))
        panel.cp_plot.grab().save(str(PACKAGE/'assets/hmi_cp_plot.png'))
        w.close()

renderer=QSvgRenderer(str(PACKAGE/'assets/avans-wordmark.svg'))
image=QImage(480,184,QImage.Format.Format_ARGB32); image.fill(QColor(0,0,0,0))
painter=QPainter(image); renderer.render(painter); painter.end()
image.save(str(PACKAGE/'assets/avans-wordmark.png'))
print('Saved real native HMI dashboard, control and analytics captures (simulation only).')
