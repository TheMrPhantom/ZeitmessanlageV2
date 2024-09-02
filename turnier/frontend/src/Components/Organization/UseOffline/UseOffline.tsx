import React, { useState } from 'react';
import { Stack, Typography, Paper } from '@mui/material';
import WarningIcon from '@mui/icons-material/Warning';
import CloudDownloadIcon from '@mui/icons-material/CloudDownload';
import CloudOffIcon from '@mui/icons-material/CloudOff';
import style from './useOffline.module.scss'
import OfflineDialog from './OfflineDialog';

type Props = {}

const UseOffline = (props: Props) => {
    const [hovered, setHovered] = useState<null | string>(null);
    const [open, setopen] = useState(false)
    const [type, settype] = useState<'online' | 'device'>('online')

    return (
        <>
            <Stack className={style.container} direction="column" gap={3} alignItems="center">
                <Typography variant="h4">Wie möchtest du fortfahren?</Typography>
                <Stack direction="row" gap={1}>
                    <WarningIcon color='warning' />
                    <Typography variant="h6">Diese Aktion kann nicht rückgängig gemacht werden</Typography>
                    <WarningIcon color='warning' />
                </Stack>
                <Stack direction="row" flexWrap="wrap" gap={3}>
                    <Paper
                        className={style.bigButton}
                        elevation={hovered === 'online' ? 15 : 3}
                        onMouseEnter={() => setHovered('online')}
                        onMouseLeave={() => setHovered(null)}
                        onClick={() => {
                            setopen(true)
                            settype("online")
                        }}
                    >
                        <Stack direction="column" gap={2}>
                            <Stack direction="row" gap={3} justifyContent="space-between" alignItems="center">
                                <Typography variant="h5">Online Daten</Typography>
                                <CloudDownloadIcon />
                            </Stack>
                            <Stack>
                                <Typography variant="h6">Lade die Daten deines Vereins herunter</Typography>
                                <Typography variant="body2">Diese Aktion löscht die aktuell auf dem Gerät gespeicherten Daten und ersetzt sie mit den Online Daten. Diese Aktion kann nicht rückgängig gemacht werden.</Typography>
                            </Stack>
                        </Stack>
                    </Paper>
                    <Paper
                        className={style.bigButton}
                        elevation={hovered === 'device' ? 15 : 3}
                        onMouseEnter={() => setHovered('device')}
                        onMouseLeave={() => setHovered(null)}
                        onClick={() => {
                            setopen(true)
                            settype("device")
                        }}
                    >
                        <Stack direction="column" gap={2}>
                            <Stack direction="row" gap={3} justifyContent="space-between" alignItems="center">
                                <Typography variant="h5">Geräte Daten</Typography>
                                <CloudOffIcon />
                            </Stack>
                            <Stack>
                                <Typography variant="h6">Verwende die Daten auf diesem Gerät</Typography>
                                <Typography variant="body2">Diese Aktion überschreibt die aktuell online gespeicherten Daten und ersetzt sie mit den Daten auf diesem Gerät. Diese Aktion kann nicht rückgängig gemacht werden.</Typography>
                            </Stack>
                        </Stack>
                    </Paper>
                </Stack>
            </Stack>
            <OfflineDialog isOpen={open} close={() => setopen(false)} type={type} />
        </>
    );
}

export default UseOffline;
