import { Button, Paper, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import React, { useState } from 'react'
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
import { addTurnament, removeTurnament } from '../../../Actions/SampleAction';
import { dateToString, dateToURLString, doRequest } from '../../Common/StaticFunctions';
import { ALL_HEIGHTS, ALL_RUNS, RunInformation, Tournament } from '../../../types/ResponseTypes';
import { loadPermanent, storePermanent } from '../../Common/StaticFunctionsTyped';
import { openToast } from '../../../Actions/CommonAction';

type Props = {}

const Dashboard = (props: Props) => {
    const params = useParams()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const t_organization = params.organization ? params.organization : ""
    loadPermanent(t_organization, dispatch, common)


    const [turnamentDate, setturnamentDate] = useState<Date | null>(new Date())
    const [judgeName, setjudgeName] = useState("")
    const [tournamentName, settournamentName] = useState("")

    return (
        <Stack gap={2} className={style.paper}>
            <Stack direction="row" flexWrap="wrap" gap={3}>
                <Typography variant='h5'>Wilkommen zurück HSF!</Typography>
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
                                        doRequest("PUT", `${t_organization}/tournament`, { date: dateToURLString(turnamentDate), judge: judgeName, name: tournamentName })
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
                                                        console.log(organization)
                                                        const newTurnaments = organization.turnaments.filter((t: any) => t.date !== turnament.date)
                                                        organization.turnaments = newTurnaments
                                                        storePermanent(t_organization, organization)
                                                        dispatch(removeTurnament(turnament))
                                                        doRequest("DELETE", `${t_organization}/tournament`, { date: dateToURLString(turnament.date) })
                                                    }
                                                }}>
                                                <DeleteIcon />
                                            </Button>
                                        </TableCell>
                                    </TableRow>
                                )
                            })}
                        </TableBody>
                    </Table>
                </TableContainer>

            </Stack>
        </Stack>
    )
}

export default Dashboard