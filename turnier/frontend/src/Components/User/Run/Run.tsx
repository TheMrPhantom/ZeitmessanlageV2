import React, { useEffect, useState } from 'react'
import style from './run.module.scss'
import { Collapse, Divider, FormControlLabel, Paper, Rating, Stack, Switch, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Typography } from '@mui/material'



import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useParams } from 'react-router-dom'
import { getRanking, getResultFromParticipant, getRunCategory, getTimeFaults, getTotalFaults, loadPermanent, maximumTime, runTimeToString, runTimeToStringClock, standardTime } from '../../Common/StaticFunctionsTyped';
import { dateToURLString } from '../../Common/StaticFunctions';
import { Run as RunType, SkillLevel, Participant, defaultParticipant, RunCategory, Result } from '../../../types/ResponseTypes';
import { changeParticipants } from '../../../Actions/SampleAction';
import { minSpeedA3 } from '../../Common/AgilityPO';
import { useCallback, useMemo } from 'react';

type Props = {}

const Run = (props: Props) => {
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const [ws, setws] = useState<WebSocket | null>(null);
    const dispatch = useDispatch()
    //const navigate = useNavigate()
    const params = useParams()

    const minTimeout = 2000;
    const maxTimeout = 5000;

    const tempdate = useMemo<Date>(() => { return params.date ? new Date(params.date) : new Date() }, [params.date])
    const tempturnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(tempdate))?.date
    const turnamentDate = useMemo(() => { return new Date(tempturnamentDate ? tempturnamentDate : tempdate) }, [tempturnamentDate, tempdate])
    const organization = params.organization ? params.organization : ""

    loadPermanent(params, dispatch, common)

    //Get all participants
    const allParticipants = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.participants
    const currentRun: RunType = params.class ? Number(params.class) : 0

    //Filter all participants for the current run
    const currentRunClass: SkillLevel = params.class ? Math.floor(Number(params.class) / 2) : 0
    const currentSize = params.class ? Number(params.size) : 0
    const participants = allParticipants?.filter(p => p.class === currentRunClass && p.size === currentSize)

    const [selectedParticipantIndex, setselectedParticipantIndex] = useState(0)
    const selectedParticipant: Participant = participants ? participants[selectedParticipantIndex] : defaultParticipant

    const currentResult = getResultFromParticipant(currentRun, selectedParticipant)

    const [newFaults, setnewFaults] = useState(-1)
    const [newRefusals, setnewRefusals] = useState(-1)
    const [showStarter, setshowStarter] = useState(true)
    const [showResults, setshowResults] = useState(true)

    const parcoursInfos = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.runs.find(r => r.run === currentRun && r.height === currentSize)

    const participantsWithResults = participants?.filter(p => getResultFromParticipant(currentRun, p).time > -2)
    const calculatedStandardTime = standardTime(currentRun,
        currentSize,
        participants ? participants : [],
        parcoursInfos?.length ? parcoursInfos?.length : 0,
        parcoursInfos?.speed ? parcoursInfos?.speed : minSpeedA3)

    const currentTime = currentResult.time
    const currentFaults = currentResult.faults
    const currentRefusals = currentResult.refusals
    const currentTimeFault = getTimeFaults(currentResult, calculatedStandardTime).toFixed(2)

    const [started, setStarted] = useState(false);
    const [initTime, setinitTime] = useState(new Date().getTime())

    const changeFaults = useCallback((value: number) => {

        if (allParticipants) {
            if (!started) {
                const newParticipants = allParticipants.map(p => {
                    if (p.startNumber === selectedParticipant.startNumber) {
                        if (getRunCategory(currentRun) === RunCategory.A) {
                            return { ...p, resultA: { ...p.resultA, faults: value } }
                        } else {
                            return { ...p, resultJ: { ...p.resultJ, faults: value } }
                        }
                    }
                    return p
                })

                dispatch(changeParticipants(turnamentDate, newParticipants))
            } else {
                setnewFaults(value)
            }
        }
    }, [currentRun, dispatch, participants, selectedParticipant.startNumber, turnamentDate, started])

    const changeRefusals = useCallback((value: number) => {
        if (allParticipants) {
            if (!started) {
                const newParticipants = allParticipants.map(p => {
                    if (p.startNumber === selectedParticipant.startNumber) {
                        if (getRunCategory(currentRun) === RunCategory.A) {
                            return { ...p, resultA: { ...p.resultA, refusals: value } }
                        } else {
                            return { ...p, resultJ: { ...p.resultJ, refusals: value } }
                        }
                    }
                    return p
                })


                dispatch(changeParticipants(turnamentDate, newParticipants))
            } else {
                setnewRefusals(value)
            }
        }
    }, [currentRun, dispatch, participants, selectedParticipant.startNumber, turnamentDate, started])

    const changeAll = useCallback((time: number, faults: number, refusals: number) => {
        if (allParticipants) {
            const newParticipants = allParticipants.map(p => {
                if (p.startNumber === selectedParticipant.startNumber) {
                    console.log(p.name)
                    if (getRunCategory(currentRun) === RunCategory.A) {
                        return { ...p, resultA: { ...p.resultA, time: time, faults: faults, refusals: refusals } }
                    } else {
                        return { ...p, resultJ: { ...p.resultJ, time: time, faults: faults, refusals: refusals } }
                    }
                }
                return p
            })

            dispatch(changeParticipants(turnamentDate, newParticipants))
        }
    }, [currentRun, dispatch, participants, selectedParticipant.startNumber, turnamentDate])

    const changeTime = useCallback(
        (value: number) => {
            if (allParticipants) {

                if (newFaults === -1 && newRefusals === -1) {
                    const newParticipants = allParticipants.map(p => {
                        if (p.startNumber === selectedParticipant.startNumber) {
                            //  check if a or j
                            if (getRunCategory(currentRun) === RunCategory.A) {
                                return { ...p, resultA: { ...p.resultA, time: value } }
                            } else {
                                return { ...p, resultJ: { ...p.resultJ, time: value } }
                            }

                        }
                        return p
                    })

                    dispatch(changeParticipants(turnamentDate, newParticipants))
                } else {
                    setnewFaults(-1)
                    setnewRefusals(-1)
                    changeAll(value, newFaults === -1 ? currentFaults : newFaults, newRefusals === -1 ? currentRefusals : newRefusals)

                }
            }
        }
        , [dispatch,
            participants,
            selectedParticipant,
            turnamentDate,
            currentRun,
            currentFaults,
            currentRefusals,
            newFaults,
            newRefusals,
            changeAll
        ])





    useEffect(() => {
        if (!started) {
            return;
        }

        var id = setInterval(() => {
            const milliseconds = new Date().getTime() - initTime;
            const seconds = Math.floor(milliseconds / 10) / 100
            changeTime(seconds);
            if (!started) {
                clearInterval(id);
            }
        }, 35);
        return () => clearInterval(id);
    }, [started, changeTime, initTime]);

    useEffect(() => {
        if (currentTime > 0) {
            if (currentTime > maximumTime(currentRun, calculatedStandardTime)) {
                //Disqualify
                changeTime(-1)
            }
        }
    }, [calculatedStandardTime, changeTime, currentRun, currentTime])

    useEffect(() => {
        setws(new WebSocket("ws://localhost:9001/ws"));

    }, [])

    useEffect(() => {
        const closeWs = () => {
            try {
                if (ws !== null) {
                    ws.close()
                }
            }
            catch (e) {
                console.log(e);
            }
        }

        if (ws !== null) {
            ws.onmessage = (e: MessageEvent) => {
                const message = JSON.parse(e.data);
                console.log(message)
                switch (message.action) {
                    case "start_timer":
                        startTimer();
                        break;
                    case "stop_timer":
                        stopTimer();
                        changeTime(message.time)
                        break;
                    case "changed_current_participant":
                        setselectedParticipantIndex(message.participant)
                        break;
                    case "changed_current_fault":
                        changeFaults(message.fault)
                        break;
                    case "changed_current_refusal":
                        changeRefusals(message.refusal)
                        break;
                }

            };

            ws.onerror = () => {
                setTimeout(() => {
                    closeWs()
                    setws(new WebSocket("ws://localhost:9001/ws"));
                }, Math.random() * (maxTimeout - minTimeout) + minTimeout);
            }

            ws.onclose = () => {
                setTimeout(() => {
                    closeWs()
                    setws(new WebSocket("ws://localhost:9001/ws"));
                }, Math.random() * (maxTimeout - minTimeout) + minTimeout);
            }

        }
    }, [ws, changeTime, changeFaults, changeRefusals])


    const startTimer = () => {

        setinitTime(new Date().getTime());
        setStarted(true)

    }


    const stopTimer = () => {
        setStarted(false)
    }


    return (
        <Stack className={style.runContainer} direction="column" alignItems="center" gap={4}>
            <Paper className={style.timeContainer}>
                <Stack gap={2} direction="column">
                    <Stack gap={1} direction="column" alignItems="flex-start">
                        <Typography variant='overline'>Aktuell am Start</Typography>
                        <Typography variant='h4'>{selectedParticipant?.name}</Typography>
                        <Typography variant='h5'>mit</Typography>
                        <Typography variant='h4'>{selectedParticipant?.dog}</Typography>
                    </Stack>
                    <Divider orientation='horizontal' flexItem />
                    <Stack direction="column" gap={2} alignItems="center" flexWrap="wrap">


                        <Stack direction="row"  >
                            <Typography variant='h2'>{runTimeToStringClock(getResultFromParticipant(currentRun, selectedParticipant).time)}</Typography>
                        </Stack>
                        <Stack direction="row" gap={2} flexWrap="wrap" justifyContent="space-between">
                            <Typography variant='h2'>✋{currentFaults}</Typography>
                            <Typography variant='h2'>✊{currentRefusals}</Typography>
                            <Typography variant='h2'>⌛{currentTimeFault}</Typography>

                        </Stack>
                    </Stack>


                </Stack>
            </Paper>
            <Stack gap={3}>
                <Stack direction="row" gap={2} justifyContent="space-between">
                    <Typography variant='h5'>Starter</Typography>
                    <FormControlLabel control={<Switch value={showStarter}
                        defaultChecked={showStarter}
                        onChange={(value) => { setshowStarter(value.target.checked) }}
                    />} label="Anzeigen" />
                </Stack>
                <Collapse in={showStarter} unmountOnExit >
                    <TableContainer component={Paper}>
                        <Table size="small">
                            <TableHead>
                                <TableRow>
                                    <TableCell>#</TableCell>
                                    <TableCell>Starter</TableCell>
                                    <TableCell>Hund</TableCell>
                                    <TableCell>Favorit</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {
                                    participants?.map((p, index) => {
                                        const result = getResultFromParticipant(currentRun, p)
                                        return (
                                            <TableRow key={p.startNumber} className={index === selectedParticipantIndex ? style.selected : ""}>
                                                <TableCell>{p.sorting}</TableCell>
                                                <TableCell>{p.name} </TableCell>
                                                <TableCell>{p.dog}</TableCell>
                                                <TableCell align="center">
                                                    <Rating max={1} />
                                                </TableCell>

                                            </TableRow>
                                        )
                                    })
                                }
                            </TableBody>
                        </Table>
                    </TableContainer>
                </Collapse>
                <Stack direction="row" gap={2} justifyContent="space-between">
                    <Typography variant='h5'>Ergebnisse</Typography>
                    <FormControlLabel control={<Switch value={showResults}
                        defaultChecked={showResults}
                        onChange={(value) => { setshowResults(value.target.checked) }} />} label="Anzeigen" />
                </Stack>
                <Collapse in={showResults} unmountOnExit >
                    <TableContainer component={Paper}>
                        <Table size="small">
                            <TableHead>
                                <TableRow>
                                    <TableCell></TableCell>
                                    <TableCell>Name</TableCell>
                                    <TableCell>Hund</TableCell>
                                    <TableCell>Zeit</TableCell>
                                    <TableCell>F</TableCell>
                                    <TableCell>V</TableCell>
                                    <TableCell>ZF</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {/*Participants with result sorted by totalfaults and time*/
                                    getRanking(participantsWithResults, currentRun, calculatedStandardTime).map((value) => {
                                        return (
                                            <TableRow key={value.participant.startNumber} className={value.participant.startNumber === selectedParticipant.startNumber ? style.selected : ""}>
                                                <TableCell>{value.rank}.</TableCell>
                                                <TableCell>{value.participant.name}</TableCell>
                                                <TableCell>{value.participant.dog}</TableCell>
                                                <TableCell>{runTimeToString(value.result.time)}</TableCell>
                                                <TableCell>{value.result.time > 0 ? value.result.faults : "-"}</TableCell>
                                                <TableCell>{value.result.time > 0 ? value.result.refusals : "-"}</TableCell>
                                                <TableCell>{value.result.time > 0 ? getTimeFaults(value.result, calculatedStandardTime).toFixed(2) : "-"}</TableCell>
                                            </TableRow>
                                        )
                                    }
                                    )
                                }
                            </TableBody>
                        </Table>
                    </TableContainer>
                </Collapse>
            </Stack>
        </Stack>
    )
}

export default Run