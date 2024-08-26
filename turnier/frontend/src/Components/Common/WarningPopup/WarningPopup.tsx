import { Button, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle } from '@mui/material'
import React from 'react'
import { ABBRECHEN, JA, NEIN, OK, WARNUNG } from '../Internationalization/i18n'
import style from './warningpopup.module.scss'
import WarningAmberIcon from '@mui/icons-material/WarningAmber';

type Props = {
    isOpen: boolean,
    close: (state: boolean) => void,
    title: string,
    text: string,
    yes?: () => void,
    no?: () => void,
    ok?: () => void,
    abort?: () => void
}

const WarningPopup = (props: Props) => {
    return (
        <Dialog open={props.isOpen} onClose={props.close}>
            <DialogTitle className={style.title} sx={{ color: "warning.light" }}><WarningAmberIcon sx={{ color: "warning.light" }} />{WARNUNG}: {props.title}</DialogTitle>
            <DialogContent>
                <DialogContentText>
                    {props.text}
                </DialogContentText>
            </DialogContent>
            <DialogActions className={style.actions}>
                {props.yes ? <Button onClick={() => {
                    props.close(false);
                    if (props.yes !== undefined) {
                        props.yes()
                    }
                }}>
                    {JA}
                </Button> : <></>}
                {props.ok ? <Button onClick={() => {
                    props.close(false);
                    if (props.ok !== undefined) {
                        props.ok()
                    }
                }}>
                    {OK}
                </Button> : <></>}
                {props.no ? <Button onClick={() => {
                    props.close(false);
                    if (props.no !== undefined) {
                        props.no()
                    }
                }}>
                    {NEIN}
                </Button> : <></>}
                {props.abort ? <Button onClick={() => {
                    props.close(false);
                    if (props.abort !== undefined) {
                        props.abort()
                    }
                }}>
                    {ABBRECHEN}
                </Button> : <></>}
            </DialogActions>
        </Dialog>
    )
}

export default WarningPopup