import os
import shutil
from pathlib import Path
from .measurements import save_json


class Storage:
    def __init__(self, base: Path, config, measurements):
        self.measurements = measurements
        self.simulated = config.simulate_usb
        self.usb = (base / 'data/simulated_usb' if self.simulated else
                    Path(config.usb_mount_path) if config.usb_mount_path else None)
        if self.simulated:
            self.usb.mkdir(parents=True, exist_ok=True)
        self.ejected = False
        self.progress = dict(state='IDLE', percent=0, message='Ready to export')

    def connected(self):
        return bool(self.usb and self.usb.is_dir() and not self.ejected and
                    (self.simulated or os.path.ismount(self.usb)))

    def status(self):
        return dict(connected=self.connected(), simulated=self.simulated,
                    usb_free_bytes=shutil.disk_usage(self.usb).free if self.connected() else 0,
                    local_free_bytes=shutil.disk_usage(self.measurements.root).free,
                    unexported=sum(m['state'] == 'COMPLETED' and not m['exported'] for m in self.measurements.list()),
                    progress=self.progress)

    def export(self, identifier):
        if not self.connected():
            raise ValueError('No USB storage connected. Local measurements remain available.')
        source = self.measurements.folder_for(identifier)
        metadata = self.measurements.detail(identifier)['metadata']
        if metadata['state'] != 'COMPLETED':
            raise ValueError('Only completed measurements can be exported')
        size = sum(p.stat().st_size for p in source.iterdir() if p.is_file())
        if shutil.disk_usage(self.usb).free < size + 1024 * 1024:
            raise ValueError('Insufficient USB free space')
        destination = self.usb / identifier
        # Re-exports retain earlier copies and never overwrite an unrelated folder.
        if destination.exists():
            from uuid import uuid4
            destination = self.usb / (identifier + '_export_' + uuid4().hex[:8])
        temporary = destination.with_name(destination.name + '.partial')
        self.progress = dict(state='EXPORTING', percent=0, message=metadata['name'])
        try:
            temporary.mkdir()
            files = [p for p in source.iterdir() if p.is_file()]
            for i, path in enumerate(files):
                shutil.copy2(path, temporary / path.name)
                with (temporary / path.name).open('r+b') as stream:
                    os.fsync(stream.fileno())
                self.progress['percent'] = round((i + 1) / len(files) * 100)
            metadata['exported'] = True
            save_json(temporary / 'experiment.json', metadata)
            temporary.rename(destination)
            save_json(source / 'experiment.json', metadata)
            self.progress = dict(state='COMPLETED', percent=100, message='Export complete; local copy retained')
        except Exception as exc:
            self.progress = dict(state='ERROR', percent=0, message=str(exc))
            raise
        return self.progress

    def eject(self):
        if not self.simulated:
            raise ValueError('Use the operating system’s safe-remove/eject command for this physical USB drive. Automatic OS unmount is not configured.')
        self.ejected = True
        return dict(message='Simulated USB safely ejected')

    def reconnect(self):
        if not self.simulated:
            raise ValueError('Reconnect the physical USB drive using the operating system')
        self.ejected = False
        return self.status()
