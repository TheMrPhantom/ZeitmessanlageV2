import { Button, InputAdornment, Paper, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import React from 'react'
import style from './turnament.module.scss'
import { Organization, Run, Size } from '../../../types/ResponseTypes'
import { classToString, getNumberOfParticipantsForRun, getNumberOfParticipantsForRunWithResult, sizeToString } from '../../Common/StaticFunctionsTyped'

import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useNavigate, useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions'
import { changeDate, changeJudge, changeLength, changeSpeed, changeTurnamentName, createOrganization, loadOrganization } from '../../../Actions/SampleAction'
import { LocalizationProvider } from '@mui/x-date-pickers/LocalizationProvider/LocalizationProvider'
import { DatePicker } from '@mui/x-date-pickers/DatePicker'
import { AdapterDateFns } from '@mui/x-date-pickers/AdapterDateFns'
import { de } from 'date-fns/locale'

type Props = {}

const Turnament = (props: Props) => {
    const runs = [Run.A3, Run.A2, Run.A1, Run.A0, Run.J3, Run.J2, Run.J1, Run.J0]
    const heights = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const params = useParams()

    const t_organization = params.organization ? params.organization : ""
    const item = window.localStorage.getItem(t_organization);

    if (item === null) {

        const organization = {
            name: t_organization,
            turnaments: []
        }
        window.localStorage.setItem(t_organization, JSON.stringify({ organization }))
        dispatch(createOrganization(organization))
    } else {
        if (common.organization.name !== t_organization) {
            const org: Organization = JSON.parse(item)
            dispatch(loadOrganization(org))
        }
    }

    const date = params.date ? new Date(params.date) : new Date()
    const turnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))?.date

    const turnament = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))

    const judgeName = turnament?.judge
    const turnamentName = turnament?.name
    const allParticipants = turnament?.participants

    console.log(turnamentDate)
    return (
        <Stack className={style.container} gap={2}>
            <Stack direction="row" flexWrap="wrap" gap={2}>
                <Stack className={style.infoBox} gap={2}>
                    <Typography variant='h5'>Datum</Typography>
                    <LocalizationProvider dateAdapter={AdapterDateFns} adapterLocale={de}>
                        <DatePicker
                            label="Turnier Datum"
                            value={new Date(turnamentDate ? turnamentDate : date)}
                            onChange={(value) => {
                                if (value) {
                                    dispatch(changeDate(date, new Date(value)))
                                    const t_organization = params.organization ? params.organization : ""
                                    if (item !== null) {
                                        window.localStorage.setItem(t_organization, JSON.stringify(common.organization))
                                    }
                                    navigate(`/o/${params.organization}/${dateToURLString(new Date(value))}`)
                                }


                            }} />
                    </LocalizationProvider>
                </Stack>

                <Stack className={style.infoBox} gap={2}>
                    <Typography variant='h5'>Turniername</Typography>
                    <TextField
                        value={turnamentName}
                        label="Turniername"
                        fullWidth
                        onChange={(value) => {
                            dispatch(changeTurnamentName(new Date(turnamentDate ? turnamentDate : date), value.target.value))
                            const t_organization = params.organization ? params.organization : ""
                            if (item !== null) {
                                window.localStorage.setItem(t_organization, JSON.stringify(common.organization))
                            }
                        }}
                    />
                </Stack>
                <Stack className={style.infoBox} gap={2}>
                    <Typography variant='h5'>Richter</Typography>
                    <TextField
                        value={judgeName}
                        label="Richter Name"
                        fullWidth
                        onChange={(value) => {
                            dispatch(changeJudge(new Date(turnamentDate ? turnamentDate : date), value.target.value))
                            const t_organization = params.organization ? params.organization : ""
                            if (item !== null) {
                                window.localStorage.setItem(t_organization, JSON.stringify(common.organization))
                            }
                        }}
                    />
                </Stack>
                <Stack className={style.infoBox} gap={2}>
                    <Typography variant='h5'>Teilnehmer</Typography>
                    <Button variant='outlined'
                        onClick={() => navigate(`/o/${params.organization}/${params.date}/participants`)}
                    >Teilnehmer bearbeiten</Button>
                </Stack>
            </Stack>
            <Stack>
                <Typography variant='h5'>Läufe</Typography>
                <Stack className={style.runTable}>
                    <TableContainer component={Paper}>
                        <Table>
                            <TableHead>
                                <TableRow>
                                    <TableCell>Lauf</TableCell>
                                    <TableCell>Länge</TableCell>
                                    <TableCell>Geschwindigkeit</TableCell>
                                    <TableCell>Zum Lauf</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {runs.map((run, index) => {
                                    return (
                                        <TableRow key={index}>
                                            <TableCell>{classToString(run)}</TableCell>
                                            <TableCell>
                                                <TextField value={common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.length}
                                                    type="number"
                                                    className={style.runStats}
                                                    InputProps={{
                                                        endAdornment: <InputAdornment position="end">m</InputAdornment>,
                                                    }}
                                                    onChange={(value) => {
                                                        const date = params.date ? new Date(params.date) : new Date()
                                                        dispatch(changeLength(date, run, Number(value.target.value)))
                                                        const t_organization = params.organization ? params.organization : ""
                                                        if (item !== null) {
                                                            window.localStorage.setItem(t_organization, JSON.stringify(common.organization))
                                                        }
                                                    }}
                                                />
                                            </TableCell>
                                            <TableCell>
                                                <TextField value={common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.speed}
                                                    type="number"
                                                    className={style.runStats}
                                                    InputProps={{
                                                        endAdornment: <InputAdornment position="end">m/s</InputAdornment>,
                                                    }}
                                                    onChange={(value) => {
                                                        const date = params.date ? new Date(params.date) : new Date()
                                                        dispatch(changeSpeed(date, run, Number(value.target.value)))
                                                        const t_organization = params.organization ? params.organization : ""
                                                        if (item !== null) {
                                                            window.localStorage.setItem(t_organization, JSON.stringify(common.organization))
                                                        }
                                                    }}
                                                />
                                            </TableCell>
                                            <TableCell>
                                                <Stack gap={2} direction="row" flexWrap="wrap">
                                                    {heights.map((height, index) => {
                                                        const countParticipants = getNumberOfParticipantsForRun(allParticipants ? allParticipants : [], run / 2, height)
                                                        const countParticipantsWithResult = getNumberOfParticipantsForRunWithResult(allParticipants ? allParticipants : [], run, height)
                                                        return <Button key={index}
                                                            variant='outlined'
                                                            color={countParticipants !== countParticipantsWithResult ? 'error' : 'primary'}
                                                            onClick={() => navigate(`/o/${params.organization}/${params.date}/${run}/${height}`)}
                                                            disabled={countParticipants === 0}
                                                        >{sizeToString(height)}</Button>
                                                    })}
                                                </Stack>
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
        </Stack>
    )
}

export default Turnament