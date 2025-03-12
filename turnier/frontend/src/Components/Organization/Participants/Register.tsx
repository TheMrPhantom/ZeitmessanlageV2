import { Button, Dialog, DialogActions, DialogContent, DialogTitle, Paper, Stack, TextField, ToggleButton, ToggleButtonGroup, Typography } from '@mui/material'
import React, { useState } from 'react'
import style from './participants.module.scss'
import HowToRegIcon from '@mui/icons-material/HowToReg';
import AlarmOnIcon from '@mui/icons-material/AlarmOn';
import PaidIcon from '@mui/icons-material/Paid';
import { Participant } from '../../../types/ResponseTypes';
import Spacer from '../../Common/Spacer';

type Props = {
    open: boolean,
    onClose: () => void,
    participants: Participant[],
    updateParticipant: (participant: Participant, type: "paid" | "registered" | "ready") => void
}

const Register = (props: Props) => {
    const [search, setsearch] = useState("")


    return (
        <Dialog open={props.open} onClose={() => { props.onClose(); setsearch("") }}  >
            <DialogTitle>
                Teilnehmer melden
            </DialogTitle>
            <DialogContent >
                <Spacer vertical={10} />
                <TextField label="Suche..." value={search} onChange={(value) => setsearch(value.target.value)} />

                <Stack flexDirection={"column"} gap={2}>
                    {props.participants.filter((p) => {
                        return p.name.toLocaleLowerCase().replaceAll(" ", "").includes(search.toLocaleLowerCase().replaceAll(" ", "")) ||
                            p.dog.toLocaleLowerCase().replaceAll(" ", "").includes(search.toLocaleLowerCase().replaceAll(" ", "")) ||
                            p.club.toLocaleLowerCase().replaceAll(" ", "").includes(search.toLocaleLowerCase().replaceAll(" ", ""))
                    }).slice(0, 5).map((participant, index) => {
                        const buttonValues: Array<string> = []
                        if (participant.paid) {
                            buttonValues.push("paid")
                        }
                        if (participant.registered) {
                            buttonValues.push("registered")
                        }
                        if (participant.ready) {
                            buttonValues.push("ready")
                        }
                        return (
                            <Paper className={style.participantPaper} elevation={5}>
                                <Stack direction="row" justifyContent={"space-between"} alignItems={"center"} gap={2}>
                                    <Stack direction="row" alignItems={"center"} gap={2}>
                                        <Typography variant='h5'>{participant.startNumber}</Typography>
                                        <Typography variant='h6'>{participant.name}</Typography>
                                        <Typography variant='body1'>{participant.dog}</Typography>
                                    </Stack >
                                    <ToggleButtonGroup value={buttonValues} >
                                        <ToggleButton
                                            value="paid"
                                            selected={buttonValues.includes("paid")}
                                            color={buttonValues.includes("paid") ? 'success' : 'error'}
                                            onClick={() => {
                                                props.updateParticipant(participant, "paid")
                                            }}
                                        >
                                            <PaidIcon />
                                        </ToggleButton>
                                        <ToggleButton
                                            value="registered"
                                            selected={buttonValues.includes("registered")}
                                            color={buttonValues.includes("registered") ? 'success' : 'error'}
                                            onClick={() => {
                                                props.updateParticipant(participant, "registered")
                                            }}
                                        >
                                            <HowToRegIcon />
                                        </ToggleButton>
                                        <ToggleButton
                                            value="ready"
                                            selected={buttonValues.includes("ready")}
                                            color={buttonValues.includes("ready") ? 'success' : 'error'}
                                            onClick={() => {
                                                props.updateParticipant(participant, "ready")
                                            }}
                                        >
                                            <AlarmOnIcon />
                                        </ToggleButton>
                                    </ToggleButtonGroup>
                                </Stack>
                            </Paper>
                        )
                    }
                    )}

                </Stack>
            </DialogContent >
            <DialogActions>
                <Stack direction="row" justifyContent="flex-end">
                    <Button onClick={() => { props.onClose(); setsearch("") }}>Schließen</Button>
                </Stack>
            </DialogActions>

        </Dialog >
    )
}

export default Register