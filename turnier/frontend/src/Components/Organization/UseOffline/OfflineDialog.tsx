import { Button, Checkbox, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle, FormControl, FormControlLabel, FormLabel, Grow, Paper, Radio, RadioGroup, Stack, Typography } from '@mui/material'
import React, { useState } from 'react'
import { classToString, getKombiRanking, getRunCategory, sizeToString } from '../../Common/StaticFunctionsTyped'
import { ExtendedResult, Organization, Participant, Run, RunCategory, Size, SkillLevel, StickerInfo, Tournament } from '../../../types/ResponseTypes'
import Spacer from '../../Common/Spacer'
import { useDispatch } from 'react-redux'
import { addPrintParticipant, addPrintResult, addPrintSticker } from '../../../Actions/SampleAction'
import { useNavigate, useParams } from 'react-router-dom'
import { ParticipantToPrint, ResultToPrint } from '../../../Reducer/CommonReducer'
import { minSpeedA3, minSpeedJ3 } from '../../Common/AgilityPO'

import WarningIcon from '@mui/icons-material/Warning';
import style from './useOffline.module.scss'


type Props = {
    type: "online" | "device",
    isOpen: boolean,
    close: () => void
}

const OfflineDialog = (props: Props) => {

    const navigate = useNavigate();

    return (
        <Dialog open={props.isOpen} onClose={props.close} sx={{ zIndex: 20000000 }} >
            <DialogTitle>{props.type === "online" ? "Onlinedaten verwenden" : "Gerätedaten verwenden"}</DialogTitle>
            <DialogContent >
                <DialogContentText>
                    {props.type === "online" ? "Bist du dir sicher, dass du die online Daten verwenden möchtest?" :
                        "Bist du dir sicher, dass du die auf dem Gerät gespeicherten Daten verwenden möchtest?"}
                </DialogContentText>
                <Spacer vertical={20} />
                <Stack direction="row" gap={2} flexWrap="wrap" >
                    <WarningIcon color='warning' />
                    <Typography variant="body1">Diese Aktion kann nicht rückgängig gemacht werden</Typography>
                    <WarningIcon color='warning' />
                </Stack>

            </DialogContent>
            <DialogActions>
                <Stack direction="row" justifyContent="space-between" className={style.buttonContainer}>
                    <Button onClick={props.close} variant='outlined'>Abbrechen</Button>
                    <Button onClick={() => {
                        props.close()
                        navigate(`/`)
                    }} variant='contained'>{props.type === "online" ? "Onlinedaten" : "Gerätedaten"}</Button>
                </Stack>
            </DialogActions>
        </Dialog >
    )
}

export default OfflineDialog