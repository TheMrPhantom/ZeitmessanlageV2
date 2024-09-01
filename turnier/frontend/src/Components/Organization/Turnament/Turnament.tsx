import { Button, InputAdornment, Paper, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import style from './turnament.module.scss'
import { ALL_HEIGHTS, ALL_RUNS, Organization, Run, Size } from '../../../types/ResponseTypes'
import { classToString, fixDis, getNumberOfParticipantsForRun, getNumberOfParticipantsForRunWithResult, getRanking, loadPermanent, setMaxTime, sizeToString, standardTime, storePermanent, updateDatabase } from '../../Common/StaticFunctionsTyped'

import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useNavigate, useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions'
import { changeDate, changeJudge, changeLength, changeParticipants, changeSpeed, changeTurnamentName, clearPrints, createOrganization, loadOrganization } from '../../../Actions/SampleAction'
import { LocalizationProvider } from '@mui/x-date-pickers/LocalizationProvider/LocalizationProvider'
import { DatePicker } from '@mui/x-date-pickers/DatePicker'
import { AdapterDateFns } from '@mui/x-date-pickers/AdapterDateFns'
import { de } from 'date-fns/locale'
import PrintingDialog from './PrintingDialog'
import { minSpeedA3 } from '../../Common/AgilityPO'

type Props = {}

const Turnament = (props: Props) => {
    const runs = [Run.A3, Run.A2, Run.A1, Run.A0, Run.J3, Run.J2, Run.J1, Run.J0]
    const heights = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const params = useParams()

    const t_organization = params.organization ? params.organization : ""

    loadPermanent(params, dispatch, common)

    const date = useMemo(() => params.date ? new Date(params.date) : new Date(), [params.date])
    const turnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))?.date

    const turnament = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))

    const judgeName = turnament?.judge
    const turnamentName = turnament?.name
    const allParticipants = turnament?.participants

    const [printDialogOpen, setprintDialogOpen] = useState(false)

    const runsDialog = [Run.A3, Run.J3, Run.A2, Run.J2, Run.A1, Run.J1, Run.A0, Run.J0]
    const heightsDialog = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]

    const fixAllParticipants = useCallback(() => {

        /* Fix max time and 3 dis */


        var tempParticipants = allParticipants ? allParticipants : []
        /* For each run execute setmaxtime */
        ALL_RUNS.forEach(run => {
            ALL_HEIGHTS.forEach(height => {
                const length = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.length
                const speed = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.speed
                const stdTime = standardTime(run, height, tempParticipants ? tempParticipants : [], length ? length : 0, speed ? speed : minSpeedA3)
                const p = tempParticipants?.filter(p => p.skillLevel === Math.floor(run / 2) && p.size === height)

                tempParticipants = setMaxTime(run, stdTime, p ? p : [], tempParticipants ? tempParticipants : [])
                tempParticipants = fixDis(run, p ? p : [], tempParticipants ? tempParticipants : [])

            })
        })



        dispatch(changeParticipants(date, tempParticipants))
        storePermanent(t_organization, common.organization)

    }, [allParticipants, common.organization, date, dispatch, params.date, t_organization])

    const rankings = runsDialog.map((run) => {
        const length = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.length
        const speed = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === run)?.speed
        return {
            run: run,
            length: length,
            heights: heightsDialog.map((height) => {
                const stdTime = standardTime(run, height, allParticipants ? allParticipants : [], length ? length : 0, speed ? speed : minSpeedA3)
                return {
                    height: height,
                    stdTime: stdTime,
                    results: getRanking(allParticipants ? allParticipants : [], run, stdTime, height)
                }
            })
        }
    })
    const hasMounted = useRef(false);

    useEffect(() => {
        if (!hasMounted.current) {
            hasMounted.current = true;
            fixAllParticipants()
        }
    }, [allParticipants, common.organization, date, dispatch, params.date, t_organization, fixAllParticipants])



    useEffect(() => {
        //get current turnament
        updateDatabase(turnament)

    }, [turnament])


    return (
        <>
            <Stack className={style.container} gap={2}>
                <Stack direction="column" gap={1}>
                    <Stack direction="row" flexWrap="wrap">
                        <Stack className={style.infoBox} gap={2}>
                            <Typography variant='h5'>Datum</Typography>
                            <LocalizationProvider dateAdapter={AdapterDateFns} adapterLocale={de}>
                                <DatePicker
                                    label="Turnier Datum"
                                    value={new Date(turnamentDate ? turnamentDate : date)}
                                    onChange={(value) => {
                                        if (value) {
                                            updateDatabase(turnament)
                                            dispatch(changeDate(date, new Date(value)))
                                            const t_organization = params.organization ? params.organization : ""

                                            storePermanent(t_organization, common.organization)

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
                                    updateDatabase(turnament)
                                    const t_organization = params.organization ? params.organization : ""

                                    storePermanent(t_organization, common.organization)

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
                                    updateDatabase(turnament)
                                    const t_organization = params.organization ? params.organization : ""

                                    storePermanent(t_organization, common.organization)

                                }}
                            />
                        </Stack>
                    </Stack>
                    <Stack direction="row" flexWrap="wrap">
                        <Stack className={style.infoBox} gap={2}>
                            <Typography variant='h5'>Teilnehmer</Typography>
                            <Button variant='outlined'
                                onClick={() => navigate(`/o/${params.organization}/${params.date}/participants`)}
                            >Teilnehmer bearbeiten</Button>
                        </Stack>
                        <Stack className={style.infoBox} gap={2}>
                            <Typography variant='h5'>Listen Drucken</Typography>
                            <Button variant='outlined'
                                onClick={() => {
                                    fixAllParticipants()
                                    dispatch(clearPrints());
                                    setprintDialogOpen(true);
                                }}
                            >Auswählen</Button>
                        </Stack>
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

                                                            storePermanent(t_organization, common.organization)

                                                        }}
                                                    />
                                                </TableCell>
                                                {((run !== Run.A3) && (run !== Run.J3)) ?
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

                                                                storePermanent(t_organization, common.organization)

                                                            }}
                                                        />
                                                    </TableCell> : <TableCell></TableCell>}

                                                <TableCell>
                                                    <Stack gap={2} direction="row" flexWrap="wrap">
                                                        {heights.map((height, index) => {
                                                            const countParticipants = getNumberOfParticipantsForRun(allParticipants ? allParticipants : [], Math.floor(run / 2), height)
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
            </Stack >
            {
                turnament ? <PrintingDialog participants={allParticipants ? allParticipants : []}
                    rankings={rankings}
                    isOpen={printDialogOpen}
                    organization={common.organization
                    }
                    turnament={turnament}
                    close={() => { setprintDialogOpen(false) }}
                /> : <></ >}

        </>
    )
}

export default Turnament