import { Button, FormControl, InputLabel, MenuItem, Paper, Select, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import React, { useState } from 'react'
import DeleteIcon from '@mui/icons-material/Delete';
import style from './participants.module.scss'
import { Participant, SkillLevel, Size } from '../../../types/ResponseTypes';
import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions';
import { addParticipant, removeParticipant } from '../../../Actions/SampleAction';
import { loadPermanent, runClassToString, sizeToString, storePermanent } from '../../Common/StaticFunctionsTyped';

type Props = {}

const Participants = (props: Props) => {

    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    //const navigate = useNavigate()
    const params = useParams()

    const tempdate = params.date ? new Date(params.date) : new Date()
    const tempturnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(tempdate))?.date
    const turnamentDate = new Date(tempturnamentDate ? tempturnamentDate : tempdate)
    const organization = params.organization ? params.organization : ""

    const [name, setname] = useState("")
    const [club, setclub] = useState("")
    const [dog, setdog] = useState("")
    const [runclass, setrunclass] = useState(SkillLevel.A3)
    const [size, setsize] = useState(Size.Small)

    loadPermanent(params, dispatch, common)

    const addParticipantToTurnament = (name: string, club: string, dog: string, runclass: SkillLevel, size: Size) => {

        const participant: Participant = {
            startNumber: 0,
            sorting: 0,
            name: name,
            club: club,
            dog: dog,
            class: runclass,
            size: size,
            resultA: {
                time: -2,
                faults: 0,
                refusals: 0,
                eliminated: false,
                class: runclass * 2
            },
            resultJ: {
                time: -2,
                faults: 0,
                refusals: 0,
                eliminated: false,
                class: runclass * 2 + 1
            }
        }

        //Get all participants from turnament
        const participants = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.participants
        //Get the highest startnumber
        const startNumber = participants && participants.length > 0 ? Math.max(...participants.map(p => p.startNumber)) : 0
        //Add 1 to the highest startnumber
        participant.startNumber = startNumber + 1

        //Get participants with same class and size
        const sameClass = participants?.filter(p => p.class === runclass && p.size === size)
        //Get the highest sorting
        const sorting = sameClass && sameClass.length > 0 ? Math.max(...sameClass.map(p => p.sorting)) : 0
        //Add 1 to the highest sorting
        participant.sorting = sorting + 1

        console.log(participant)

        //Add the new participant to the participants
        dispatch(addParticipant(turnamentDate, participant))

        setname("")
        setclub("")
        setdog("")
        setrunclass(SkillLevel.A3)
        setsize(Size.Small)

        //Store the new participant in the local storage
        storePermanent(organization, common.organization)
    }

    return (
        <Stack gap={2} className={style.paper}>
            <Stack direction="row" flexWrap="wrap" gap={3}>
                <Paper className={style.paper}>
                    <Stack gap={2}>
                        <Typography variant='h6'>Teilnehmer aus Webmelden importieren</Typography>
                        <input type="file" name="file" onChange={(value) => {
                            // In drinklist nachschauen wie das tut
                        }}
                        />
                        <Button variant="contained" color="primary">Hochladen</Button>
                    </Stack>
                </Paper>
                <Paper className={style.paper}>
                    <Stack gap={2}>
                        <Typography variant='h6'>Teilnehmer manuell hinzufügen</Typography>
                        <Stack direction="row" flexWrap="wrap" gap={2}>
                            <TextField value={name} label="Name" onChange={(value) => setname(value.target.value)} />
                            <TextField value={club} label="Verein" onChange={(value) => setclub(value.target.value)} />
                            <TextField value={dog} label="Hundename" onChange={(value) => setdog(value.target.value)} />
                            <FormControl className={style.picker}>
                                <InputLabel id="demo-simple-select-label" >Klasse</InputLabel>
                                <Select
                                    labelId="demo-simple-select-label"
                                    id="demo-simple-select"
                                    value={runclass}
                                    label="Klasse"
                                    onChange={(value) => setrunclass(Number(value.target.value))}
                                >
                                    <MenuItem value={SkillLevel.A0}>A0</MenuItem>
                                    <MenuItem value={SkillLevel.A1}>A1</MenuItem>
                                    <MenuItem value={SkillLevel.A2}>A2</MenuItem>
                                    <MenuItem value={SkillLevel.A3}>A3</MenuItem>
                                </Select>
                            </FormControl>
                            <FormControl className={style.picker}>
                                <InputLabel id="demo-simple-select-label" >Größe</InputLabel>
                                <Select
                                    labelId="demo-simple-select-label"
                                    id="demo-simple-select"
                                    value={size}
                                    label="Größe"
                                    onChange={(value) => setsize(Number(value.target.value))}
                                >
                                    <MenuItem value={Size.Small}>Small</MenuItem>
                                    <MenuItem value={Size.Medium}>Medium</MenuItem>
                                    <MenuItem value={Size.Intermediate}>Intermediate</MenuItem>
                                    <MenuItem value={Size.Large}>Large</MenuItem>
                                </Select>
                            </FormControl>


                        </Stack>
                        <Button variant="contained"
                            color="primary"
                            onClick={() => {
                                addParticipantToTurnament(name, club, dog, runclass, size)
                            }}
                        >Hinzufügen</Button>
                    </Stack>
                </Paper>
            </Stack>
            <Stack gap={2}>
                <Typography variant='h5'>Starter</Typography>
                <TableContainer component={Paper} className={style.participantTable}>
                    <Table size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell>Startnummer</TableCell>
                                <TableCell>Name</TableCell>
                                <TableCell>Verein</TableCell>
                                <TableCell>Hund</TableCell>
                                <TableCell>Klasse</TableCell>
                                <TableCell>Größe</TableCell>
                                <TableCell>Entfernen</TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>

                            {common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.participants.map((participant, index) => {
                                return (
                                    <TableRow key={index}>
                                        <TableCell>{participant.startNumber}</TableCell>
                                        <TableCell>{participant.name}</TableCell>
                                        <TableCell>{participant.club}</TableCell>
                                        <TableCell>{participant.dog}</TableCell>
                                        <TableCell>{runClassToString(participant.class)}</TableCell>
                                        <TableCell>{sizeToString(participant.size)}</TableCell>
                                        <TableCell>
                                            <Button color='error' variant="outlined" onClick={() => {
                                                dispatch(removeParticipant(turnamentDate, participant))
                                                //Store the new participants in the local storage
                                                storePermanent(organization, common.organization)
                                            }}>
                                                <DeleteIcon />
                                            </Button>
                                        </TableCell>
                                    </TableRow>
                                )
                            })
                            }
                        </TableBody>
                    </Table>
                </TableContainer>

            </Stack>
        </Stack>
    )
}

export default Participants