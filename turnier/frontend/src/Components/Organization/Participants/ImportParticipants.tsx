import { Button, Dialog, DialogActions, DialogContent, DialogTitle, FormControl, FormControlLabel, InputLabel, MenuItem, Paper, Radio, RadioGroup, Select, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Typography } from '@mui/material'
import React, { useCallback, useEffect, useState } from 'react'
import { useParams } from 'react-router-dom'
import { useDispatch, useSelector } from 'react-redux';
import { changeParticipants } from '../../../Actions/SampleAction';
import { runClassToString, sizeToString, storePermanent, updateDatabase } from '../../Common/StaticFunctionsTyped';
import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { Participant, Size, SkillLevel } from '../../../types/ResponseTypes';
import style from './participants.module.scss'
import { dateToString, dateToURLString } from '../../Common/StaticFunctions';

type Props = {
    tournaments: Array<{ date: string, participants: Participant[] }> | null,
    close: () => void
}

const ImportParticipants = (props: Props) => {
    const params = useParams();
    const dateParam = params.date
    const dateToChange = dateToURLString(new Date(dateParam ? dateParam : ""))
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()

    const [selectedDate, setselectedDate] = useState(0)
    const [selectedVariant, setselectedVariant] = useState(1)
    const [participantsToUpload, setparticipantsToUpload] = useState<Participant[]>([])
    const [participantPreview, setparticipantPreview] = useState<Participant[]>([])

    const generateNewParticipantsList = useCallback(() => {
        let participantsToOverrite: Participant[] = []

        const t = props.tournaments
        if (t === null) {
            return
        }

        const dateParam = params.date
        //Copy old participants
        const oldParticipantsTemp = common.organization?.turnaments.find((tournament) => dateToURLString(new Date(tournament.date)) === dateToURLString(new Date(dateParam ? dateParam : "")))?.participants
        if (oldParticipantsTemp === undefined) {
            return
        }
        const oldParticipants = oldParticipantsTemp.map((participant) => {
            return { ...participant }
        })

        //Get new participants
        const newParticipants = t[selectedDate].participants

        /* Merge the participants lists */
        if (selectedVariant === 0) {

            /* Get new participants list that contains only the new participants */
            participantsToOverrite = newParticipants.filter((newParticipant) => {
                return !oldParticipants.some((oldParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
            })

            /* Get the old participants list that contains only the new participants */
            const oldParticipantsToKeep = oldParticipants.filter((oldParticipant) => {
                return newParticipants.some((newParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
            })

            /* change skill and size of old participants to new skill and size */
            oldParticipantsToKeep.forEach((oldParticipant) => {
                const newParticipant = newParticipants.find((newParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
                const newSkillLevel = newParticipant?.skillLevel
                const newSize = newParticipant?.size
                oldParticipant.skillLevel = newSkillLevel !== undefined ? newSkillLevel : SkillLevel.A0
                oldParticipant.size = newSize !== undefined ? newSize : Size.Small
            })

            participantsToOverrite = [...participantsToOverrite, ...oldParticipantsToKeep]


        } else if (selectedVariant === 1) {
            participantsToOverrite = newParticipants
            /*Add old participants that are not part of new participants */
            const oldParticipantsToAdd = oldParticipants.filter((oldParticipant) => {
                return !newParticipants.some((newParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
            })

            participantsToOverrite = [...participantsToOverrite, ...oldParticipantsToAdd]

        } else if (selectedVariant === 2) {
            participantsToOverrite = newParticipants
            /*Add old participants that are not part of new participants */
            const oldParticipantsToAdd = oldParticipants.filter((oldParticipant) => {
                return !newParticipants.some((newParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
            })

            /*Set all old participants to DIS */
            oldParticipantsToAdd.forEach((participant) => {
                participant.resultA.time = -1;
                participant.resultJ.time = -1;
            })

            participantsToOverrite = [...participantsToOverrite, ...oldParticipantsToAdd]
        }

        participantsToOverrite = participantsToOverrite.map((participant, index) => {
            return { ...participant, startNumber: index + 1 }
        })

        setparticipantsToUpload(participantsToOverrite)

    }, [common.organization?.turnaments, params.date, props.tournaments, selectedDate, selectedVariant])

    useEffect(() => {
        generateNewParticipantsList()
    }, [generateNewParticipantsList])

    useEffect(() => {

        const oldParticipants = common.organization?.turnaments.find((tournament) => dateToURLString(new Date(tournament.date)) === dateToChange)?.participants
        if (oldParticipants) {
            const participantsToAdd = oldParticipants.filter((newParticipant) => {
                return !participantsToUpload.some((oldParticipant) => {
                    return oldParticipant.name === newParticipant.name && oldParticipant.dog === newParticipant.dog
                })
            })
            setparticipantPreview([...participantsToUpload, ...participantsToAdd])
        }

    }, [common.organization?.turnaments, dateToChange, participantsToUpload])


    const sendNewParticipants = () => {
        if (participantsToUpload.length > 0) {

            /* Give all participants new start numbers */

            dispatch(changeParticipants(new Date(dateToChange), participantsToUpload))
            storePermanent(common.organization.name, common.organization)

            const tournamentToUpdate = common.organization?.turnaments.find((tournament) => dateToURLString(new Date(tournament.date)) === dateToChange)
            updateDatabase(tournamentToUpdate, common.organization.name, dispatch)
        }
    }

    const oldParticipants = common.organization?.turnaments.find((tournament) => dateToURLString(new Date(tournament.date)) === dateToChange)?.participants
    const addedCount = participantPreview.filter((participant) => {
        return !oldParticipants?.some((oldParticipant) => {
            return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
        })
    }).length

    const deletedCount = participantPreview.filter((participant) => {
        return !participantsToUpload?.some((oldParticipant) => {
            return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
        })
    }).length

    const changedCount = participantPreview.filter((participant) => {
        /* Check if size changed from old to new */
        const oldSize = oldParticipants?.find((oldParticipant) => {
            return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
        })?.size

        /* Check if skill level changed from old to new */
        const oldSkillLevel = oldParticipants?.find((oldParticipant) => {
            return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
        })?.skillLevel

        return (oldSize !== undefined && (oldSize !== participant.size)) || (oldSkillLevel !== undefined && (oldSkillLevel !== participant.skillLevel))
    }).length

    return (
        <Dialog open={props.tournaments !== null} onClose={props.close}  >
            <DialogTitle>
                <Stack direction="row" gap={2} flexWrap="wrap">
                    <Typography variant='h4'>Teilnehmer des Turniertags:</Typography>
                    <Stack direction="row" flex="wrap" gap={1}>
                        <Typography variant='h4' color="secondary">+{addedCount}</Typography>
                        <Typography variant='h4' color="orange">~{changedCount}</Typography>
                        <Typography variant='h4' color="error">-{deletedCount}</Typography>
                    </Stack>
                </Stack>
            </DialogTitle>
            <DialogContent >
                <Stack direction="column" gap={3}>
                    <Typography variant='h5'>{props.tournaments?.length} Turnier(e) Erkannt</Typography>
                    <FormControl fullWidth>
                        <InputLabel >Turniertag</InputLabel>
                        <Select
                            label="Turniertag"
                            value={selectedDate}
                            onChange={(e) => {
                                setselectedDate(Number(e.target.value))
                            }}
                        >
                            {props.tournaments?.map((tournament, index) => {
                                return <MenuItem key={index} value={index}>{dateToString(new Date(tournament.date))}</MenuItem>
                            })
                            }
                        </Select>
                    </FormControl>
                    <FormControl>
                        <Typography variant='h6'>Wie möchtest du die Teilnehmer importieren?</Typography>
                        <RadioGroup
                            row
                            value={selectedVariant}
                            onChange={(e) => {

                                setselectedVariant(Number(e.target.value))
                            }}
                        >
                            <FormControlLabel value={0} control={<Radio />} label="Daten überschreiben" />
                            <FormControlLabel value={1} control={<Radio />} label="Nur neue Meldungen importieren" />
                            <FormControlLabel value={2} control={<Radio />} label="Neue Meldungen hinzufügen + nicht mehr vorhandene auf DIS setzen" />
                        </RadioGroup>
                    </FormControl>
                    <Typography variant='h5'>Vorschau der neuen Daten</Typography>
                    <TableContainer component={Paper} className={style.participantTable}>
                        <Table size="small">
                            <TableHead>
                                <TableRow>
                                    <TableCell>Name</TableCell>
                                    <TableCell>Verein</TableCell>
                                    <TableCell>Hund</TableCell>
                                    <TableCell>Klasse</TableCell>
                                    <TableCell>Größe</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {participantPreview.map((participant, index) => {
                                    const oldParticipants = common.organization?.turnaments.find((tournament) => dateToURLString(new Date(tournament.date)) === dateToChange)?.participants
                                    /*Check if participant existed before input (present in common) */

                                    const isNew = !oldParticipants?.some((oldParticipant) => {
                                        return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
                                    })

                                    /*search participant in to upload */
                                    const isDeleted = !participantsToUpload.some((oldParticipant) => {
                                        return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
                                    })


                                    let backgroundColor = null
                                    if (isNew) {
                                        backgroundColor = "rgba(0, 255, 0, 0.1)"
                                    } else if (isDeleted) {
                                        backgroundColor = "rgba(255, 0, 0, 0.1)"
                                    }

                                    /* Check if size changed from old to new */
                                    const oldSize = oldParticipants?.find((oldParticipant) => {
                                        return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
                                    })?.size
                                    const changedSize = oldSize !== undefined && (oldSize !== participant.size)

                                    /* Check if skill level changed from old to new */
                                    const oldSkillLevel = oldParticipants?.find((oldParticipant) => {
                                        return oldParticipant.name === participant.name && oldParticipant.dog === participant.dog
                                    })?.skillLevel
                                    const changedSkillLevel = oldSkillLevel !== undefined && (oldSkillLevel !== participant.skillLevel)

                                    const classToDisplay = !changedSkillLevel ? runClassToString(participant.skillLevel) :
                                        runClassToString(oldSkillLevel) + " → " + runClassToString(participant.skillLevel)

                                    const sizeToDisplay = !changedSize ? sizeToString(participant.size) :
                                        sizeToString(oldSize) + " → " + sizeToString(participant.size)

                                    return <TableRow key={index} sx={{ backgroundColor: backgroundColor }}>
                                        <TableCell>{participant.name}</TableCell>
                                        <TableCell>{participant.club}</TableCell>
                                        <TableCell>{participant.dog}</TableCell>
                                        <TableCell sx={{ backgroundColor: changedSkillLevel ? "rgba(255, 200, 0, 0.5)" : null }}>{classToDisplay}</TableCell>
                                        <TableCell sx={{ backgroundColor: changedSize ? "rgba(255, 200, 0, 0.5)" : null }}>{sizeToDisplay}</TableCell>
                                    </TableRow>
                                })}
                            </TableBody>
                        </Table>
                    </TableContainer>
                </Stack>

            </DialogContent>
            <DialogActions>
                <Stack direction="row" justifyContent="flex-end">
                    <Button onClick={() => props.close()}>Abbrechen</Button>
                    <Button variant='outlined' onClick={() => { sendNewParticipants(); props.close() }}>Importieren</Button>
                </Stack>
            </DialogActions>

        </Dialog >
    )
}

export default ImportParticipants