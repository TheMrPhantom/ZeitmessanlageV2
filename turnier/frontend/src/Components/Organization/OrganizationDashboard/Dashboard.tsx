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
import { addTurnament, createOrganization, loadOrganization, removeTurnament } from '../../../Actions/SampleAction';
import { dateToString, dateToURLString } from '../../Common/StaticFunctions';
import { ALL_HEIGHTS, ALL_RUNS, Organization, RunInformation, Tournament } from '../../../types/ResponseTypes';

type Props = {}

const Dashboard = (props: Props) => {
    const params = useParams()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const t_organization = params.organization ? params.organization : ""
    const item = window.localStorage.getItem(t_organization);

    if (item === null) {
        const organization = {
            name: t_organization,
            turnaments: []
        }
        window.localStorage.setItem(t_organization, JSON.stringify(organization))
        dispatch(createOrganization(organization))
    } else {
        if (common.organization.name !== t_organization) {
            const org: Organization = JSON.parse(item)
            console.log(org)
            dispatch(loadOrganization(org))
        }

    }



    const [turnamentDate, setturnamentDate] = useState<Date | null>(new Date())
    const [judgeName, setjudgeName] = useState("")

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
                                if (turnamentDate !== null && judgeName !== "") {

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
                                        name: "",
                                        runs: runs
                                    }
                                    const item = window.localStorage.getItem(t_organization);
                                    if (item !== null) {
                                        const organization = JSON.parse(item)
                                        organization.turnaments.push(turnament)
                                        window.localStorage.setItem(t_organization, JSON.stringify(organization))
                                        dispatch(addTurnament(turnament))
                                    }
                                    setturnamentDate(null);
                                    setjudgeName("")
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
                                                        const newTurnaments = organization.organization.turnaments.filter((t: any) => t.date !== turnament.date)
                                                        organization.organization.turnaments = newTurnaments
                                                        window.localStorage.setItem(t_organization, JSON.stringify(organization))
                                                        dispatch(removeTurnament(turnament))
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