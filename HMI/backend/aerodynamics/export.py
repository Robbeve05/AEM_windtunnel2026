"""Headless, cross-platform chart and engineering-report exports."""
import csv
from pathlib import Path
from threading import Lock
from xml.sax.saxutils import escape

from ..measurements import save_json

_plot_lock = Lock()  # Matplotlib is not thread-safe across concurrent analyses.


def _csv(path, fields, rows):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)


def plot_cp(results, destination, *, format='png'):
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_agg import FigureCanvasAgg
    with _plot_lock:
        figure = Figure(figsize=(9, 5.2), layout='constrained')
        FigureCanvasAgg(figure)
        axis = figure.subplots()
        colors = ('#c8291e', '#2868a0', '#38906a', '#9a5c9d', '#b58128', '#35516a')
        plotted = False
        for index, result in enumerate(results):
            for surface, style in (('upper', '-'), ('lower', '--')):
                points = sorted([p for p in result['cp'] if p['surface'] == surface and p['cp'] is not None],
                                key=lambda p: p['x_c'])
                if not points:
                    continue
                label = f"{result['experiment_name']} | {result['alpha_deg']:g} deg | {surface}"
                if result['simulated']:
                    label += ' [SIM]'
                axis.errorbar([p['x_c'] for p in points], [p['cp'] for p in points],
                    yerr=[p['cp_std'] or 0 for p in points], fmt='o'+style,
                    color=colors[index % len(colors)], label=label, markersize=4, linewidth=1.5, capsize=2)
                plotted = True
        if not plotted:
            raise ValueError('No valid Cp values available for graph export')
        axis.set_xlim(0, 1)
        axis.invert_yaxis()
        axis.set_xlabel('Normalized chord position x/c [-]')
        axis.set_ylabel('Pressure coefficient Cp [-]')
        axis.grid(True, color='#e3e3e3')
        axis.legend(fontsize=8)
        title = 'Cp distribution' if len(results) > 1 else results[0]['experiment_name']
        axis.set_title(title+'\nMeasured tap positions; piecewise-linear connections; error bars: Cp standard deviation', fontsize=11)
        figure.savefig(destination, format=format, dpi=180)


def report_pdf(result, metadata, destination, chart=None):
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import getSampleStyleSheet
    from reportlab.lib.units import mm
    from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image, KeepTogether
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    from matplotlib import get_data_path
    fonts = Path(get_data_path())/'fonts'/'ttf'
    if 'AvansSans' not in pdfmetrics.getRegisteredFontNames():
        pdfmetrics.registerFont(TTFont('AvansSans', str(fonts/'DejaVuSans.ttf')))
        pdfmetrics.registerFont(TTFont('AvansSans-Bold', str(fonts/'DejaVuSans-Bold.ttf')))
        pdfmetrics.registerFontFamily('AvansSans', normal='AvansSans', bold='AvansSans-Bold')
    styles = getSampleStyleSheet()
    for style in styles.byName.values():
        style.fontName = 'AvansSans-Bold' if style.name in ('Title', 'Heading1', 'Heading2') else 'AvansSans'
    styles['Title'].textColor = colors.HexColor('#c8291e')
    styles['BodyText'].fontSize = 9
    styles['BodyText'].leading = 13
    paragraph = lambda text, style='BodyText': Paragraph(escape(str(text)), styles[style])
    label = 'SIMULATION - SYNTHETIC DATA' if result['simulated'] else 'PHYSICAL ACQUISITION'
    story = [paragraph('AVANS | WINDTUNNEL 2026', 'Title'),
             paragraph('Aerodynamic analysis report', 'Heading1'), paragraph(label, 'Heading2'),
             paragraph(result['experiment_name']), paragraph('Experiment: '+result['experiment_id']),
             paragraph('Analysis '+result['analysis_version']+' | '+result['generated_at']), Spacer(1, 5*mm)]
    summary = result['summary']
    formatted = lambda value, unit='': 'Unavailable' if value is None else f'{value:.4g} {unit}'.strip()
    data = [['Test conditions', 'Value'], ['Airfoil', result['airfoil']],
            ['Angle of attack', formatted(result['alpha_deg'], 'deg')],
            ['Chord', formatted(result['chord_mm'], 'mm')], ['Timestamp', metadata['timestamp']],
            ['Duration', formatted(summary['duration_s'], 's')],
            ['Mean airspeed', formatted(summary['mean_airspeed_ms'], 'm/s')],
            ['Air density', formatted(summary['air_density_kg_m3'], 'kg/m3')],
            ['Dynamic pressure', formatted(summary['mean_dynamic_pressure_pa'], 'Pa')],
            ['Actual Reynolds number', formatted(summary.get('actual_reynolds'))],
            ['Valid samples / taps', f"{summary['valid_samples']} / {result['quality']['valid_taps']}"],
            ['Analysis / sensor status', result['status']+' / '+result['quality']['sensor_validity']]]
    table = Table([[paragraph(cell) for cell in row] for row in data], colWidths=[65*mm, 105*mm])
    table.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#f3e7e5')),
                              ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                              ('GRID', (0, 0), (-1, -1), .3, colors.HexColor('#dddddd')),
                              ('BOTTOMPADDING', (0, 0), (-1, -1), 5)]))
    story.append(table)
    if chart and Path(chart).is_file():
        story.extend([Spacer(1, 5*mm), Image(str(chart), width=170*mm, height=98*mm)])
    coefficients = [['Coefficient', 'Value', 'Status']]
    for key, coefficient in result['coefficients'].items():
        coefficients.append([key.capitalize(), formatted(coefficient['value']), coefficient['status']])
    coefficient_table = Table([[paragraph(cell) for cell in row] for row in coefficients], colWidths=[45*mm, 60*mm, 65*mm])
    coefficient_table.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#f3e7e5')),
        ('GRID', (0, 0), (-1, -1), .3, colors.HexColor('#dddddd')), ('BOTTOMPADDING', (0, 0), (-1, -1), 5)]))
    story.append(KeepTogether([paragraph('Pressure integration', 'Heading2'), coefficient_table]))
    for key, coefficient in result['coefficients'].items():
        if coefficient['value'] is None or coefficient['status'] != 'FULL':
            story.append(paragraph(f'{key.capitalize()}: {coefficient["reason"]}'))
    story.extend([paragraph('Method, quality and limitations', 'Heading2'), paragraph(result['averaging']),
                  paragraph(result['method']), paragraph(result.get('coordinates', 'No valid integration interval'))])
    for warning in result['quality']['warnings']+result['assumptions']:
        story.append(paragraph('- '+warning))
    configuration = metadata['configuration']
    story.extend([paragraph('Measurement configuration and reproducibility', 'Heading2'),
        paragraph(f"Requested averaging: {configuration['averaging_s']:g} s | sampling: {configuration['sample_rate_hz']:g} Hz | enabled taps: {summary['tap_count']}"),
        paragraph('Description: '+(configuration['description'] or 'None')),
        paragraph('Geometry: '+metadata.get('geometry_source', 'Operator-supplied coordinates')),
        paragraph('Analysis settings: '+str(result['settings'])),
        paragraph('Revision: '+str(result.get('revision', 1))+' | Raw SHA-256: '+result.get('raw_sha256', 'Unavailable'))])
    story.append(paragraph('This report is a sectional pressure analysis. It is not a safety certificate or a validated synthetic-airfoil performance claim.'))
    def footer(canvas, document):
        canvas.setFont('AvansSans', 8)
        canvas.setFillColor(colors.HexColor('#747474'))
        canvas.drawString(20*mm, 12*mm, 'Avans Aerospace Engineering | '+label)
        canvas.drawRightString(190*mm, 12*mm, f'Page {document.page}')
    SimpleDocTemplate(str(destination), pagesize=A4, leftMargin=20*mm, rightMargin=20*mm,
                      topMargin=17*mm, bottomMargin=20*mm).build(story, onFirstPage=footer, onLaterPages=footer)


def write_exports(folder, result, metadata):
    folder.mkdir(parents=True, exist_ok=True)
    save_json(folder/'analysis.json', result)
    if result['cp']:
        _csv(folder/'processed_data.csv', list(result['cp'][0]), result['cp'])
    valid = [point for point in result['cp'] if point['cp'] is not None]
    if valid:
        fields = ['tap_id', 'source', 'surface', 'x_c', 'z_c', 'mean_pressure_pa', 'reference_pressure_pa',
                  'dynamic_pressure_pa', 'cp', 'pressure_std_pa', 'cp_std', 'valid_sample_count']
        _csv(folder/'cp_curve.csv', fields, valid)
        plot_cp([result], folder/'cp_distribution.png')
        plot_cp([result], folder/'cp_distribution.svg', format='svg')
    if any(value['value'] is not None for value in result['coefficients'].values()):
        row = dict(experiment_id=result['experiment_id'], airfoil=result['airfoil'],
            alpha_deg=result['alpha_deg'], mean_airspeed_ms=result['summary']['mean_airspeed_ms'],
            calculation_status=result['status'], analysis_version=result['analysis_version'], source=result['source'])
        row.update({key: result['summary'].get(key) for key in ('requested_reynolds', 'actual_reynolds',
            'temperature_c', 'dynamic_viscosity_pa_s', 'applied_airspeed_setpoint_ms')})
        row.update(chord_mm=result['chord_mm'], air_density_kg_m3=result['summary']['air_density_kg_m3'],
                   measured_airspeed_ms=result['summary']['mean_airspeed_ms'])
        row.update({key: value['value'] for key, value in result['coefficients'].items()})
        row.update({key+'_status': value['status'] for key, value in result['coefficients'].items()})
        _csv(folder/'coefficients.csv', list(row), [row])
    report_pdf(result, metadata, folder/'analysis_report.pdf', folder/'cp_distribution.png')
