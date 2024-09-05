import React, { useEffect, useState } from 'react'
import style from './run.module.scss'
import { Button, Divider, IconButton, InputAdornment, Paper, Stack, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, TextField, Typography } from '@mui/material'
import ArrowUpwardIcon from '@mui/icons-material/ArrowUpward';
import ArrowDownwardIcon from '@mui/icons-material/ArrowDownward';
import CloseIcon from '@mui/icons-material/Close';


import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { useParams } from 'react-router-dom'
import { doPostRequest, getRanking, getResultFromParticipant, getRunCategory, getTimeFaults, loadPermanent, maximumTime, moveParticipantInStartList, runTimeToString, runTimeToStringClock, standardTime, storePermanent, updateDatabase, wait } from '../../Common/StaticFunctionsTyped';
import { dateToURLString } from '../../Common/StaticFunctions';
import { Run as RunType, SkillLevel, Participant, defaultParticipant, RunCategory } from '../../../types/ResponseTypes';
import { changeLength, changeParticipants, changeSpeed } from '../../../Actions/SampleAction';
import { minSpeedA3 } from '../../Common/AgilityPO';
import { useCallback, useMemo } from 'react';
import SaveIcon from '@mui/icons-material/Save';
import { openToast } from '../../../Actions/CommonAction';

type Props = {
    startSerial: () => void,
    timeError: boolean,
    connected: boolean,
    setconnected: (value: boolean) => void
    lastMessage: string | null
    setlastMessage: (value: string | null) => void
    timeMeasurementActive: boolean
    settimeMeasurementActive: (value: boolean) => void
}

const Run = (props: Props) => {
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()
    //const navigate = useNavigate()
    const params = useParams()

    const tempdate = useMemo<Date>(() => { return params.date ? new Date(params.date) : new Date() }, [params.date])
    const tempturnamentDate = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(tempdate))?.date
    const turnamentDate = useMemo(() => { return new Date(tempturnamentDate ? tempturnamentDate : tempdate) }, [tempturnamentDate, tempdate])
    const organization = params.organization ? params.organization : ""
    const turnament = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))

    loadPermanent(organization, dispatch, common)

    //Get all participants
    const allParticipants = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.participants
    const currentRun: RunType = params.class ? Number(params.class) : 0

    //Filter all participants for the current run
    const currentRunClass: SkillLevel = params.class ? Math.floor(Number(params.class) / 2) : 0
    const currentSize = params.class ? Number(params.size) : 0
    const participants = allParticipants?.filter(p => p.skillLevel === currentRunClass && p.size === currentSize)

    // Get index of first participant without result
    const firstIndex = participants?.findIndex(p => getResultFromParticipant(currentRun, p).time === -2)
    // Set the selected participant as the first without result
    const [selectedParticipantStartNumber, setselectedParticipantStartNumber] = useState(firstIndex === -1 || !firstIndex ? 0 : firstIndex)
    const selectedParticipantUnsafe = participants && participants.length > 0 ? participants.find(p => p.startNumber === selectedParticipantStartNumber) : defaultParticipant
    const selectedParticipant: Participant = selectedParticipantUnsafe ? selectedParticipantUnsafe : defaultParticipant

    const currentResult = getResultFromParticipant(currentRun, selectedParticipant)

    const [newFaults, setnewFaults] = useState(-1)
    const [newRefusals, setnewRefusals] = useState(-1)

    const currentTime = currentResult.time
    const currentFaults = currentResult.faults
    const currentRefusals = currentResult.refusals

    //Timer
    const [started, setStarted] = useState(false);
    const [initTime, setinitTime] = useState(new Date().getTime())

    const parcoursInfos = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(turnamentDate))?.runs.find(r => r.run === currentRun && r.height === currentSize)

    const participantsWithResults = participants?.filter(p => getResultFromParticipant(currentRun, p).time > -2)
    const calculatedStandardTime = standardTime(currentRun,
        currentSize,
        participants ? participants : [],
        parcoursInfos?.length ? parcoursInfos?.length : 0,
        parcoursInfos?.speed ? parcoursInfos?.speed : minSpeedA3)

    const currentTimeFault = getTimeFaults(currentResult, calculatedStandardTime)

    useEffect(() => {
        doPostRequest(`${common.organization.name}/current/participant`, {
            id: selectedParticipant.startNumber, faults: currentFaults, refusals: currentRefusals, started: started, time: initTime, currentRun: currentRun
        }, dispatch)
    }, [selectedParticipant.startNumber, currentFaults, currentRefusals, common.organization.name, started, initTime, currentRun, dispatch]);

    const changeFaults = (value: number) => {

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
                storePermanent(organization, common.organization)
            } else {
                setnewFaults(value)
            }
            doPostRequest(`${common.organization.name}/current/faults`, value, dispatch)
        }
    }

    const changeRefusals = (value: number) => {
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
                storePermanent(organization, common.organization)
            } else {
                setnewRefusals(value)
            }
            doPostRequest(`${common.organization.name}/current/refusals`, value, dispatch)
        }
    }

    const changeAll = useCallback((time: number, faults: number, refusals: number) => {
        if (allParticipants) {
            const newParticipants = allParticipants.map(p => {
                if (p.startNumber === selectedParticipant.startNumber) {
                    if (getRunCategory(currentRun) === RunCategory.A) {
                        return { ...p, resultA: { ...p.resultA, time: time, faults: faults, refusals: refusals } }
                    } else {
                        return { ...p, resultJ: { ...p.resultJ, time: time, faults: faults, refusals: refusals } }
                    }
                }
                return p
            })

            dispatch(changeParticipants(turnamentDate, newParticipants))
            storePermanent(organization, common.organization)
        }
    }, [common.organization, currentRun, dispatch, organization, selectedParticipant.startNumber, turnamentDate, allParticipants])

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
                    storePermanent(organization, common.organization)
                } else {
                    setnewFaults(-1)
                    setnewRefusals(-1)
                    changeAll(value, newFaults === -1 ? currentFaults : newFaults, newRefusals === -1 ? currentRefusals : newRefusals)

                }
            }
        }
        , [allParticipants, changeAll, common.organization, currentFaults, currentRefusals, currentRun, dispatch, newFaults, newRefusals, organization, selectedParticipant.startNumber, turnamentDate
        ])





    //The timer
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

        var id = setInterval(() => {

            doPostRequest(`${common.organization.name}/current/participant`, {
                id: selectedParticipant.startNumber, faults: currentFaults, refusals: currentRefusals, started: started, time: initTime, currentRun: currentRun
            }, dispatch)

        }, 3000);
        return () => clearInterval(id);
    }, [selectedParticipant.startNumber, currentFaults, currentRefusals, common.organization.name, started, initTime, currentRun, dispatch]);




    const startTimer = useCallback(() => {
        if (currentTime === -2 || currentTime === 0) {
            setinitTime(new Date().getTime());
            setStarted(true)
            doPostRequest(`${common.organization.name}/timer`, { action: "start" }, dispatch)
        }
    }, [currentTime, common.organization.name, dispatch])


    const stopTimer = useCallback((time?: number) => {
        if (!started) {
            return;
        }
        setStarted(false)
        if (time === undefined) {
            doPostRequest(`${common.organization.name}/timer`, { action: "stop", time: currentTime }, dispatch).then(() => {
                updateDatabase(turnament, common.organization.name, dispatch)
            })
        } else {
            doPostRequest(`${common.organization.name}/timer`, { action: "stop", time: Math.floor(time / 10) / 100 }, dispatch).then(() => {
                updateDatabase(turnament, common.organization.name, dispatch)
            })
            changeTime(Math.floor(time / 10) / 100)
        }

    }, [currentTime, started, changeTime, turnament, common.organization.name, dispatch])

    const [speedWarning, setspeedWarning] = useState(false)
    const [lengthWarning, setlengthWarning] = useState(false)
    const [tempLength, settempLength] = useState(300)
    const [tempSpeed, settempSpeed] = useState(1)

    useEffect(() => {
        const pLength = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === currentRun)?.length
        const pSpeed = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === params.date)?.runs.find(r => r.run === currentRun)?.speed
        const date = params.date ? new Date(params.date) : new Date()


        if (pLength === 0) {
            setlengthWarning(true)

            dispatch(changeLength(date, currentRun, tempLength))
            const t_organization = params.organization ? params.organization : ""
            storePermanent(t_organization, common.organization)
        }

        if (pSpeed === 0 && currentRun !== RunType.J3 && currentRun !== RunType.A3) {
            setspeedWarning(true)

            dispatch(changeSpeed(date, currentRun, tempSpeed))
            const t_organization = params.organization ? params.organization : ""
            storePermanent(t_organization, common.organization)
        }

    }, [common.organization, currentRun, dispatch, params.date, params.organization, tempLength, tempSpeed])


    useEffect(() => {
        if (props.lastMessage !== null) {

            props.setlastMessage(null)
            props.setconnected(true)
            if (props.lastMessage === "start") {
                props.settimeMeasurementActive(true)
                startTimer()
            } else if (props.lastMessage.startsWith("stop")) {
                props.settimeMeasurementActive(false)
                if (started) {
                    const t = Number(props.lastMessage.substring(4))
                    stopTimer()

                    wait(100).then(() => {
                        changeTime(Math.floor(t / 10) / 100)
                        wait(1000).then(() => {
                            const nextStarterSorting = (selectedParticipant.sorting + 1) % (participants ? participants.length : 0)
                            const nextStarter = participants?.find(p => p.sorting === nextStarterSorting)
                            setselectedParticipantStartNumber(nextStarter?.startNumber ? nextStarter.startNumber : 0)

                            //setselectedParticipantIndex((selectedParticipant.sorting + 1) % (participants ? participants.length : 0))
                        })
                    })
                }

            } else if (props.lastMessage === "reset") {
                props.settimeMeasurementActive(false)
                if (started) {
                    stopTimer()

                    wait(100).then(() => {
                        changeTime(-1)
                        wait(1000).then(() => {
                            const nextStarterSorting = (selectedParticipant.sorting + 1) % (participants ? participants.length : 0)
                            const nextStarter = participants?.find(p => p.sorting === nextStarterSorting)
                            setselectedParticipantStartNumber(nextStarter?.startNumber ? nextStarter.startNumber : 0)
                        })
                    })
                }
            }

        }
    }, [props, startTimer, stopTimer, changeTime, participants, selectedParticipantStartNumber, started, selectedParticipant.sorting])


    const showWarnings = () => {
        let warnings = []

        if (lengthWarning) {

            warnings.push(<Paper className={style.error}>
                <Stack direction="column">
                    <Stack gap={1} direction="column" >
                        <Typography variant='h5'>Warnung: Parcourlänge nicht gesetzt</Typography>
                        <Typography variant='overline'>Die Parcourlänge wurde automatisch auf 300m angepasst, bitte korrekte Länge eintragen</Typography>
                        <Stack direction="row" gap={3}>
                            <TextField value={tempLength}
                                type="number"
                                className={style.runStats}

                                InputProps={{
                                    endAdornment: <InputAdornment position="end">m</InputAdornment>,
                                }}
                                onChange={(value) => {
                                    settempLength(Number(value.target.value))
                                }}

                            />
                            <Button variant='outlined' onClick={() => {
                                const date = params.date ? new Date(params.date) : new Date()
                                dispatch(changeLength(date, currentRun, tempLength))
                                const t_organization = params.organization ? params.organization : ""
                                storePermanent(t_organization, common.organization)
                                setlengthWarning(false)
                            }}>
                                <SaveIcon />
                            </Button>
                        </Stack>
                    </Stack>
                </Stack>
            </Paper >)
        }
        if (speedWarning) {
            warnings.push(<Paper className={style.error}>
                <Stack direction="column">
                    <Stack gap={1} direction="column" >
                        <Typography variant='h5'>Warnung: Parcourgeschwindigkeit nicht gesetzt</Typography>
                        <Typography variant='overline'>Die Parcourgeschwindigkeit wurde automatisch auf 1m/s angepasst, bitte korrekte Geschwindigkeit eintragen</Typography>
                        <Stack direction="row" gap={3}>
                            <TextField value={tempSpeed}
                                type="number"
                                className={style.runStats}
                                InputProps={{
                                    endAdornment: <InputAdornment position="end" variant='outlined'>m/s</InputAdornment>,
                                }}
                                onChange={(value) => {
                                    settempSpeed(Number(value.target.value))
                                }}
                            />
                            <Button variant='outlined' onClick={() => {
                                const date = params.date ? new Date(params.date) : new Date()
                                dispatch(changeSpeed(date, currentRun, tempSpeed))
                                const t_organization = params.organization ? params.organization : ""
                                storePermanent(t_organization, common.organization)
                                setspeedWarning(false)
                            }}>
                                <SaveIcon />
                            </Button>
                        </Stack>
                    </Stack>
                </Stack>
            </Paper>)
        }
        if (warnings.length > 0) {
            return <>
                <Divider orientation='horizontal' flexItem />
                {warnings}
            </>
        } else {
            return <></>
        }
    }

    useEffect(() => {
        if (currentTime > 0) {

            if (currentTime > maximumTime(currentRun, calculatedStandardTime)) {
                //Disqualify
                stopTimer()
                changeTime(-1)
            }
        }
    }, [calculatedStandardTime, changeTime, currentRun, currentTime, stopTimer])


    const clickRow = (p: Participant) => {
        if (!started) {
            setselectedParticipantStartNumber(p.startNumber)
        } else {
            dispatch(openToast({ message: "Bitte Timer stoppen", type: "warning", headline: "Teilnehmer kann nicht gewechselt werden solange der timer läuft" }))
        }
    }

    return (
        <Stack className={style.runContainer} direction="column" alignItems="center" gap={4}>
            <Paper className={style.timeContainer}>
                <Stack gap={2} direction="column">
                    <Stack direction="column">
                        <Typography variant='overline'>Aktuell am Start</Typography>
                        <Stack gap={1} direction="row" alignItems="flex-end">
                            <Typography variant='h4'>{selectedParticipant?.name}</Typography>
                            <Typography variant='h5'>mit</Typography>
                            <Typography variant='h4'>{selectedParticipant?.dog}</Typography>
                        </Stack>
                    </Stack>
                    {showWarnings()}
                    <Divider orientation='horizontal' flexItem />
                    <Stack direction="row" gap={2} justifyContent="space-between" flexWrap="wrap">

                        <Stack gap={1} className={style.timeBoxLeft} >
                            <Stack direction="row" gap={2} flexWrap="wrap" justifyContent="space-between">
                                <Stack direction="column" justifyContent="center">
                                    <Typography variant='h1'>{runTimeToStringClock(getResultFromParticipant(currentRun, selectedParticipant).time)}</Typography>
                                </Stack>
                                <Stack gap={1} direction="row" flexWrap="wrap">

                                    <Paper className={style.buttonPaper} elevation={9}>
                                        <Stack gap={1}>
                                            <Typography variant='overline'>Zeitmessanlage</Typography>
                                            <Button variant='contained' color="success" disabled={(!props.timeMeasurementActive) || props.timeError} onClick={() => { stopTimer() }}>Aktiv</Button>
                                            <Button variant='contained' color="warning" disabled={(props.timeMeasurementActive) || props.timeError} onClick={() => { startTimer() }}>Bereit</Button>
                                            <Button variant='contained' color="error" disabled={!props.timeError || props.connected}>Error</Button>
                                            <Button variant='contained'
                                                color="info"
                                                disabled={props.connected}
                                                onClick={() => {
                                                    props.startSerial()
                                                }}
                                            >{!props.connected ? "Verbinden" : "Verbunden"}</Button>
                                        </Stack>
                                    </Paper>
                                    {!props.connected ?
                                        <Paper className={style.buttonPaper} elevation={9}>
                                            <Stack gap={1}>
                                                <Typography variant='overline'>Timer</Typography>
                                                <Button variant='contained' disabled={started} onClick={() => startTimer()}>Start</Button>
                                                <Button variant='contained' disabled={!started} onClick={() => stopTimer()}>Stop</Button>
                                            </Stack>
                                        </Paper> : <></>
                                    }
                                </Stack>
                            </Stack>
                            <Divider orientation='horizontal' flexItem />
                            <Stack direction="row" justifyContent="space-evenly">
                                <Button variant='outlined' className={style.btn} onClick={() => {
                                    changeFaults(currentFaults + 1)
                                    updateDatabase(turnament, common.organization.name, dispatch)
                                }}>Fehler</Button>
                                <Button variant='outlined' className={style.btn} onClick={() => {
                                    changeRefusals(currentRefusals + 1)
                                    updateDatabase(turnament, common.organization.name, dispatch)
                                }}>Verweigerung</Button>
                            </Stack>
                        </Stack>
                        <Divider orientation='vertical' flexItem />
                        <Stack direction="column" gap={3} className={style.timeBoxRight} alignItems="center">
                            <IconButton className={style.button}
                                onClick={() => {
                                    if (participants) {
                                        //Only if timer is started otherwise show warning
                                        if (!started) {
                                            const prevStarterSortingUnsafe = (selectedParticipant.sorting - 1) % (participants ? participants.length : 0)
                                            const prevStarterSorting = prevStarterSortingUnsafe < 0 ? participants.length - 1 : prevStarterSortingUnsafe

                                            const prevStarter = participants?.find(p => p.sorting === prevStarterSorting)
                                            setselectedParticipantStartNumber(prevStarter?.startNumber ? prevStarter.startNumber : 0)
                                        } else {
                                            dispatch(openToast({ message: "Bitte Timer stoppen", type: "warning", headline: "Teilnehmer kann nicht gewechselt werden solange der timer läuft" }))
                                        }
                                    }
                                }}
                            >
                                <ArrowUpwardIcon />
                            </IconButton>
                            <Button color='error' variant="outlined" onClick={() => {
                                const stopRoutine = async () => {
                                    stopTimer()
                                    await wait(100)
                                    changeAll(-1, 0, 0)
                                }
                                stopRoutine()
                            }}>
                                <CloseIcon />
                            </Button>
                            <IconButton className={style.button}
                                onClick={() => {
                                    if (participants) {
                                        if (!started) {
                                            const nextStarterSorting = (selectedParticipant.sorting + 1) % (participants ? participants.length : 0)
                                            const nextStarter = participants?.find(p => p.sorting === nextStarterSorting)
                                            setselectedParticipantStartNumber(nextStarter?.startNumber ? nextStarter.startNumber : 0)
                                            //doPostRequest("0/current/participant", participants[nextIndex].startNumber)
                                        } else {
                                            dispatch(openToast({ message: "Bitte Timer stoppen", type: "warning", headline: "Teilnehmer kann nicht gewechselt werden solange der timer läuft" }))
                                        }
                                    }
                                }}
                            >
                                <ArrowDownwardIcon />
                            </IconButton>
                        </Stack>
                    </Stack>
                    <Divider orientation='horizontal' flexItem />
                    <Stack direction="row" gap={3} flexWrap="wrap">
                        <TextField
                            className={style.runStats}
                            type='number'
                            value={Math.max(getResultFromParticipant(currentRun, selectedParticipant).time, 0)}
                            InputProps={{
                                endAdornment: <InputAdornment position="end">s</InputAdornment>,
                            }}
                            onChange={(value) => {
                                if (Number(value.target.value) > -1) {
                                    if (Number(value.target.value) === 0) {
                                        changeTime(-2)
                                        updateDatabase(turnament, common.organization.name, dispatch)
                                    } else {
                                        changeTime(Number(value.target.value))
                                        updateDatabase(turnament, common.organization.name, dispatch)
                                    }

                                }
                            }} />
                        <TextField className={style.runStats} value={getResultFromParticipant(currentRun, selectedParticipant).faults}
                            type="number"
                            InputProps={{
                                endAdornment: <InputAdornment position="end">Fehler</InputAdornment>,
                            }}
                            onChange={(value) => {
                                if (Number(value.target.value) > -1) {
                                    changeFaults(Number(value.target.value))
                                    updateDatabase(turnament, common.organization.name, dispatch)
                                }
                            }} />
                        <TextField className={style.runStats} value={getResultFromParticipant(currentRun, selectedParticipant).refusals}
                            type="number"
                            InputProps={{
                                endAdornment: <InputAdornment position="end">Verweigerungen</InputAdornment>,
                            }}
                            onChange={(value) => {
                                if (Number(value.target.value) > -1) {
                                    changeRefusals(Number(value.target.value))
                                    updateDatabase(turnament, common.organization.name, dispatch)
                                }
                            }}
                        />
                        <TextField className={style.runStats} value={currentTimeFault} InputProps={{
                            endAdornment: <InputAdornment position="end">Zeitfehler</InputAdornment>,
                        }} disabled />
                    </Stack>
                </Stack>
            </Paper>

            <Stack gap={3}>
                <Typography variant='h5'>Starter</Typography>
                <TableContainer component={Paper}>
                    <Table size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell>Reihenfolge</TableCell>
                                <TableCell>Startnummer</TableCell>
                                <TableCell>Name</TableCell>
                                <TableCell>Hund</TableCell>
                                <TableCell>Zeit</TableCell>
                                <TableCell>F</TableCell>
                                <TableCell>V</TableCell>
                                <TableCell>ZF</TableCell>
                                <TableCell>Verschieben</TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            {
                                participants?.sort((a, b) => a.sorting - b.sorting).map((p, index) => {
                                    const result = getResultFromParticipant(currentRun, p)
                                    return (
                                        <TableRow
                                            key={p.startNumber}
                                            className={p.startNumber === selectedParticipantStartNumber ? style.selected : style.starterTableRow}
                                        >
                                            <TableCell onClick={() => { clickRow(p) }} >{p.sorting}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{p.startNumber}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{p.name}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{p.dog}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{runTimeToString(result.time)}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{result.time > 0 ? result.faults : "-"}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{result.time > 0 ? result.refusals : "-"}</TableCell>
                                            <TableCell onClick={() => { clickRow(p) }} >{result.time > 0 ? getTimeFaults(result, calculatedStandardTime).toFixed(2) : "-"}</TableCell>
                                            <TableCell>
                                                <Stack direction="row" gap={2}>
                                                    <IconButton onClick={() => {
                                                        if (allParticipants) {
                                                            dispatch(changeParticipants(turnamentDate, moveParticipantInStartList("down", currentRun, currentSize, allParticipants, p.startNumber)))
                                                            updateDatabase(turnament, common.organization.name, dispatch)
                                                            storePermanent(organization, common.organization)
                                                        }
                                                    }}>
                                                        <ArrowDownwardIcon />
                                                    </IconButton>
                                                    <IconButton onClick={() => {
                                                        if (allParticipants) {

                                                            dispatch(changeParticipants(turnamentDate, moveParticipantInStartList("up", currentRun, currentSize, allParticipants, p.startNumber)))
                                                            updateDatabase(turnament, common.organization.name, dispatch)
                                                            storePermanent(organization, common.organization)
                                                        }
                                                    }}>
                                                        <ArrowUpwardIcon />
                                                    </IconButton>
                                                </Stack>
                                            </TableCell>
                                        </TableRow>
                                    )
                                })
                            }
                        </TableBody>
                    </Table>
                </TableContainer>
                <Typography variant='h5'>Ergebnisse</Typography>
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
                                        <TableRow onClick={() => { clickRow(value.participant) }}
                                            key={value.participant.startNumber} className={value.participant.startNumber === selectedParticipant.startNumber ? style.selected : ""}>
                                            <TableCell>{value.rank > 0 ? `${value.rank}.` : ""}</TableCell>
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
            </Stack>
        </Stack>
    )
}

export default Run