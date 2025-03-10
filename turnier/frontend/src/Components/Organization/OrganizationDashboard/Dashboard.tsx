import { Accordion, AccordionDetails, AccordionSummary, Button, Paper, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import React, { useEffect, useState } from 'react'
import style from './dashboard.module.scss'
import DeleteIcon from '@mui/icons-material/Delete';
import { useNavigate, useParams } from 'react-router-dom';
import { DatePicker } from '@mui/x-date-pickers/DatePicker';
import { LocalizationProvider } from '@mui/x-date-pickers/LocalizationProvider/LocalizationProvider';
import { AdapterDateFns } from '@mui/x-date-pickers/AdapterDateFns';
import de from 'date-fns/locale/de';
import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { addTurnament, changeParticipants, removeTurnament } from '../../../Actions/SampleAction';
import { dateToString, dateToURLString } from '../../Common/StaticFunctions';
import { ALL_HEIGHTS, ALL_RUNS, RunInformation, Tournament } from '../../../types/ResponseTypes';
import { doGetRequest, doRequest, loadPermanent, storePermanent, updateDatabase } from '../../Common/StaticFunctionsTyped';
import { openToast } from '../../../Actions/CommonAction';
import ExpandMoreIcon from '@mui/icons-material/ExpandMore';
import Spacer from '../../Common/Spacer';
import MergeIcon from '@mui/icons-material/Merge';
import NorthEastIcon from '@mui/icons-material/NorthEast';
import SouthEastIcon from '@mui/icons-material/SouthEast';
import { fillStartNumbers } from '../Participants/ImportParticipants';

type Props = {}

const Dashboard = (props: Props) => {
    const params = useParams()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const t_organization = params.organization ? params.organization : ""
    loadPermanent(t_organization, dispatch, common)

    useEffect(() => {

        doGetRequest("organization/" + t_organization, dispatch).then((data) => {
            if (data.code === 200) {
                setorganizerAlias(data.content.name)
            }
        }
        )
    }, [dispatch, t_organization])


    const [organizerAlias, setorganizerAlias] = useState("")
    const [turnamentDate, setturnamentDate] = useState<Date | null>(new Date())
    const [judgeName, setjudgeName] = useState("")
    const [tournamentName, settournamentName] = useState("")
    const [startnumberTransferOpen, setstartnumberTransferOpen] = useState(false)
    const [fromStartNumer, setfromStartNumer] = useState("")
    const [toStartNumber, settoStartNumber] = useState("")

    const startNumberButtonText = (turnament: Tournament) => {
        const date = dateToString(new Date(turnament.date))
        if (date === fromStartNumer) {
            return <NorthEastIcon />
        } else if (date === toStartNumber) {
            return <SouthEastIcon />
        }
        return "Auswählen"
    }

    const setFromOrTo = (turnament: Tournament) => {
        const date = dateToString(new Date(turnament.date))

        //Unckeck if already checked
        if (date === fromStartNumer) {
            setfromStartNumer("")
        } else if (date === toStartNumber) {
            settoStartNumber("")
        } else {
            if (fromStartNumer === "") {
                setfromStartNumer(date)
            } else if (toStartNumber === "") {
                settoStartNumber(date)
            }
        }
    }

    const fromAndToSelected = () => {
        return fromStartNumer !== "" && toStartNumber !== ""
    }

    const isSelected = (turnament: Tournament) => {
        const date = dateToString(new Date(turnament.date))
        return date === fromStartNumer || date === toStartNumber
    }

    const transferStartNumbers = () => {
        if (fromAndToSelected()) {
            const from = common.organization.turnaments.find((t: Tournament) => dateToString(new Date(t.date)) === fromStartNumer)
            const to = common.organization.turnaments.find((t: Tournament) => dateToString(new Date(t.date)) === toStartNumber)

            //Check if both tournaments are found
            if (from !== undefined && to !== undefined) {

                //Check if some of the participants of to has already results
                const hasResults = to.participants.some((p) => p.resultA.time !== -2 || p.resultJ.time !== -2)

                if (!hasResults) {

                    //Delete startnumbers from 'to'
                    to.participants.forEach((p) => {
                        p.startNumber = -1
                    })

                    // For each participant in from add the startnumber to the participant in to
                    from.participants.forEach((p) => {
                        const participant = to.participants.find((p2) => p2.name === p.name && p2.dog === p.dog)
                        if (participant !== undefined) {
                            participant.startNumber = p.startNumber
                        }
                    })


                    console.log(to.participants)

                    // Fill the rest of the startnumbers in 'to'
                    fillStartNumbers(to.participants)

                    //Update the organization
                    dispatch(changeParticipants(new Date(toStartNumber), from.participants))
                    storePermanent(common.organization.name, common.organization)
                    updateDatabase(to, common.organization.name, dispatch)

                    dispatch(openToast({ message: "Startnummern erfolgreich übertragen", type: "success" }))
                } else {
                    console.log(to.participants)
                    dispatch(openToast({ message: "Das Zielturnier hat bereits Ergebnisse eingetragen", type: "error" }))
                }
            } else {
                dispatch(openToast({ message: "Ein oder beide Turniere konnten nicht gefunden werden", type: "error" }))
            }
        } else {
            dispatch(openToast({ message: "Bitte ein Start- und ein Zielturnier auswählen", type: "error" }))
        }
    }


    return (
        <Stack gap={2} className={style.paper}>
            <Stack direction="row" flexWrap="wrap" gap={3}>
                <Typography variant='h5'>Wilkommen zurück {organizerAlias}!</Typography>
            </Stack>
            <Stack direction="row" flexWrap="wrap" gap={3}>
                <Paper className={style.paper}>
                    <Stack gap={2}>
                        <Typography variant='h6'>Neues Turnier Anlegen</Typography>
                        <Stack direction="row" flexWrap="wrap" gap={2}>
                            <LocalizationProvider dateAdapter={AdapterDateFns} adapterLocale={de}>
                                <DatePicker
                                    label="Turnier Datum"
                                    value={turnamentDate}
                                    onChange={(value) => {
                                        setturnamentDate(value)
                                    }} />
                            </LocalizationProvider>
                            <TextField
                                value={tournamentName}
                                label="Turniername"
                                onChange={(value) => {
                                    settournamentName(value.target.value)
                                }}
                            />
                            <TextField
                                value={judgeName}
                                label="Richter"
                                onChange={(value) => {
                                    setjudgeName(value.target.value)
                                }}
                            />


                        </Stack>
                        <Button
                            variant="contained"
                            color="primary"
                            onClick={() => {
                                if (turnamentDate !== null && judgeName !== "" && tournamentName !== "") {

                                    const runs: RunInformation[] = []

                                    ALL_RUNS.forEach(element => {
                                        ALL_HEIGHTS.forEach(height => {
                                            runs.push({ run: element, height: height, length: 0, speed: 0 })
                                        });
                                    });

                                    const turnament: Tournament = {
                                        date: turnamentDate,
                                        judge: judgeName,
                                        participants: [],
                                        name: tournamentName,
                                        runs: runs
                                    }
                                    const item = window.localStorage.getItem(t_organization);
                                    if (item !== null) {
                                        const organization = JSON.parse(item)
                                        organization.turnaments.push(turnament)
                                        storePermanent(t_organization, organization)
                                        dispatch(addTurnament(turnament))
                                        doRequest("PUT", `${t_organization}/tournament`, { date: dateToURLString(turnamentDate), judge: judgeName, name: tournamentName }, dispatch)
                                    }
                                    setturnamentDate(null);
                                    setjudgeName("")
                                    settournamentName("")
                                } else {
                                    //Show warning Toast depending on what is missing
                                    if (turnamentDate === null) {
                                        dispatch(openToast({ message: "Bitte ein Datum auswählen", type: "warning" }))
                                    } else if (judgeName === "") {
                                        dispatch(openToast({ message: "Bitte einen Richter eintragen", type: "warning" }))
                                    } else if (tournamentName === "") {
                                        dispatch(openToast({ message: "Bitte einen Turniernamen eintragen", type: "warning" }))
                                    }

                                }
                            }
                            }
                        >Anlegen</Button>
                    </Stack>
                </Paper>
            </Stack>
            <Stack gap={2}>
                <Typography variant='h5'>Aktive Turniere</Typography>
                <TableContainer component={Paper} className={style.participantTable}>
                    <Table size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell>Datum</TableCell>
                                <TableCell>Richter</TableCell>
                                <TableCell>Teilnehmer</TableCell>
                                <TableCell>Zum Turnier</TableCell>
                                <TableCell>Löschen</TableCell>
                                {startnumberTransferOpen ?
                                    <TableCell>
                                        Startnummber übertragen
                                    </TableCell> : <></>}
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            {common.organization.turnaments.map((turnament, index) => {
                                return (
                                    <TableRow key={index}>
                                        <TableCell>{dateToString(new Date(turnament.date))}</TableCell>
                                        <TableCell>{turnament.judge}</TableCell>
                                        <TableCell>{turnament.participants.length}</TableCell>
                                        <TableCell>
                                            <Button variant="outlined"
                                                onClick={() => {
                                                    navigate(`/o/${t_organization}/${dateToURLString(new Date(turnament.date))}`)
                                                }}>
                                                Zum Turnier
                                            </Button>
                                        </TableCell>
                                        <TableCell>
                                            <Button color='error'
                                                variant="outlined"
                                                onClick={() => {
                                                    const item = window.localStorage.getItem(t_organization);
                                                    if (item !== null) {
                                                        const organization = JSON.parse(item)
                                                        const newTurnaments = organization.turnaments.filter((t: any) => t.date !== turnament.date)
                                                        organization.turnaments = newTurnaments
                                                        storePermanent(t_organization, organization)
                                                        dispatch(removeTurnament(turnament))
                                                        doRequest("DELETE", `${t_organization}/tournament`, { date: dateToURLString(turnament.date) }, dispatch)
                                                    }
                                                }}>
                                                <DeleteIcon />
                                            </Button>
                                        </TableCell>
                                        {startnumberTransferOpen ?
                                            <TableCell>
                                                <Button
                                                    variant={isSelected(turnament) ? "contained" : "outlined"}
                                                    disabled={fromAndToSelected() && !isSelected(turnament)}

                                                    onClick={() => {
                                                        setFromOrTo(turnament)
                                                    }}>
                                                    {startNumberButtonText(turnament)}
                                                </Button>
                                            </TableCell> : <></>}
                                    </TableRow>
                                )
                            })}
                        </TableBody>
                    </Table>
                </TableContainer>

            </Stack>
            <Stack gap={2}>
                <Typography variant='h5'>Funktionen</Typography>
                {/* Function for transfering starter numbers from one tournament to another */}
                <Accordion expanded={startnumberTransferOpen} onChange={(_, expanded) => { setstartnumberTransferOpen(expanded) }}>
                    <AccordionSummary
                        expandIcon={<ExpandMoreIcon />}
                    >
                        <Typography component="span">Startnummern übertragen</Typography>
                    </AccordionSummary>
                    <AccordionDetails>
                        <Typography variant='caption'>
                            Wähle zwei Turniere von oben aus, um die Startnummern zu übertragen.
                        </Typography>
                        <Spacer vertical={10} />
                        <Stack flexDirection={"row"} flexWrap={"wrap"} alignItems={"center"} gap={2}>
                            <TextField label="von" value={fromStartNumer} InputProps={{ readOnly: true }} />
                            <MergeIcon style={{ transform: 'rotate(90deg)' }} />
                            <TextField label="nach" value={toStartNumber} InputProps={{ readOnly: true }} />
                            <Button
                                variant="contained"
                                disabled={!fromAndToSelected()}
                                onClick={() => {
                                    transferStartNumbers()
                                    setfromStartNumer("")
                                    settoStartNumber("")
                                    setstartnumberTransferOpen(false)


                                }}
                            >
                                Übertragen
                            </Button>
                        </Stack>
                    </AccordionDetails>
                </Accordion>
            </Stack>
        </Stack>
    )
}

export default Dashboard