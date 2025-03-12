import React, { useEffect, useRef, useState } from 'react'
import style from './run.module.scss'
import { Collapse, Divider, FormControlLabel, Paper, Rating, Stack, Switch, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Typography } from '@mui/material'
import { useParams } from 'react-router-dom'
import { checkIfFavorite, doGetRequest, favoriteIdenfitier, getKombiRanking, getParticipantsForRun, getRanking, getResultFromParticipant, getRunCategory, getTimeFaults, maximumTime, runTimeToString, runTimeToStringClock, standardTime } from '../../Common/StaticFunctionsTyped';
import { Run as RunType, SkillLevel, Participant, defaultParticipant, RunCategory, Tournament } from '../../../types/ResponseTypes';
import { minSpeedA3 } from '../../Common/AgilityPO';
import { useCallback } from 'react';
import Spacer from '../../Common/Spacer';
import { useDispatch } from 'react-redux';

type Props = {}

const Run = (props: Props) => {
    //const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const [websocket, setwebsocket] = useState<WebSocket | null>(null);
    const dispatch = useDispatch()

    const params = useParams()

    const isKombi = window.location.pathname.endsWith("kombi")

    const [common, setcommon] = useState<Tournament>({
        date: new Date(),
        judge: "",
        name: "",
        participants: [],
        runs: []
    })

    //Get all participants
    let allParticipants = common.participants

    const first = useRef<Array<Participant>>([])
    if (allParticipants.length > 0) {
        first.current = allParticipants;
    } else {
        allParticipants = first.current;
    }

    const currentRun: RunType = params.class ? Number(params.class) : 0

    //Filter all participants for the current run
    const currentRunClass: SkillLevel = params.class ? Math.floor(Number(params.class) / 2) : 0
    const currentSize = params.class ? Number(params.size) : 0
    const participants = getParticipantsForRun(allParticipants, currentRunClass, currentSize)

    const [selectedParticipantStartNumber, setselectedParticipantStartNumber] = useState(0)
    const unsafeselectedParticipant = participants?.find(p => p.startNumber === selectedParticipantStartNumber)
    const selectedParticipant: Participant = unsafeselectedParticipant ? unsafeselectedParticipant : defaultParticipant


    const currentResult = getResultFromParticipant(currentRun, selectedParticipant)

    const [newFaults, setnewFaults] = useState(-1)
    const [newRefusals, setnewRefusals] = useState(-1)
    const [started, setStarted] = useState(false);
    const [reload, setreload] = useState(false)

    const newFaultsR = useRef(newFaults)
    newFaultsR.current = newFaults

    const newRefusalsR = useRef(newRefusals)
    newRefusalsR.current = newRefusals

    const startedR = useRef(false)
    startedR.current = started

    const reloadR = useRef(reload)
    reloadR.current = reload

    const timerStaredRef = useRef(false)


    const [showStarter, setshowStarter] = useState(true)
    const [showResults, setshowResults] = useState(true)

    const parcoursInfos = common.runs.find(r => r.run === currentRun && r.height === currentSize)

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

    const refusalToDisplay = useRef(currentRefusals)
    const faultsToDisplay = useRef(currentFaults)
    const selectedRun = useRef(-1)

    const [initTime, setinitTime] = useState(new Date().getTime())

    const changeFaults = useCallback((value: number) => {

        if (allParticipants.length > 0) {

            if (!startedR.current) {

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

                const t = { ...common }
                t.participants = newParticipants
                console.log(allParticipants, newParticipants, t)
                //setcommon(t)
                setreload(!reload)
                faultsToDisplay.current = value

            } else {

                setnewFaults(value)
                faultsToDisplay.current = value
            }
        }
    }, [currentRun, selectedParticipant.startNumber, allParticipants, common, reload])


    const changeRefusals = useCallback((value: number) => {
        if (allParticipants) {
            if (!startedR.current) {
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


                const t = { ...common }
                t.participants = newParticipants
                setcommon(t)
                refusalToDisplay.current = value
            } else {
                setnewRefusals(value)
                refusalToDisplay.current = value
            }
        }
    }, [currentRun, selectedParticipant.startNumber, allParticipants, common])

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

            const t = { ...common }
            t.participants = newParticipants
            setcommon(t)
        }
    }, [currentRun, selectedParticipant.startNumber, allParticipants, common])

    const changeTime = useCallback(
        (value: number) => {
            if (allParticipants) {

                if (newFaultsR.current === -1 && newRefusalsR.current === -1) {
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

                    const t = { ...common }
                    t.participants = newParticipants
                    setcommon(t)
                } else {
                    setnewFaults(-1)
                    setnewRefusals(-1)
                    changeAll(value, newFaultsR.current === -1 ? currentFaults : newFaultsR.current, newRefusalsR.current === -1 ? currentRefusals : newRefusalsR.current)

                }
            }
        }
        , [
            selectedParticipant,
            currentRun,
            currentFaults,
            currentRefusals,
            changeAll,
            allParticipants,
            common
        ])

    //changed
    useEffect(() => {
        doGetRequest(`${params.organization}/tournament/${params.secret}/${params.date}`, dispatch).then((data) => {
            if (data.code === 200) {
                setcommon(data.content as Tournament)
            }
        }, dispatch)
    }, [reload, params.date, params.organization, params.secret, dispatch])

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
                stopTimer()
            }
        }
    }, [calculatedStandardTime, changeTime, currentRun, currentTime])


    const updateData = useCallback(() => {
        doGetRequest(`${params.organization}/tournament/${params.secret}/${params.date}`, dispatch).then((data) => {
            if (data.code === 200) {
                const t = data.content as Tournament
                setcommon(t)
            }
        })
    }, [dispatch, params.date, params.organization, params.secret])


    const ref = useRef(true)
    useEffect(() => {
        if (ref.current) {
            ref.current = false;
            const ws = new WebSocket(window.globalTS.WEBSOCKET)

            ws.onopen = () => {
                ws.send(JSON.stringify({
                    action: "subscribe",
                    organization: params.organization
                }))
            }

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

            ws.onmessage = (e: MessageEvent) => {
                const message = JSON.parse(e.data);

                switch (message.action) {
                    case "start_timer":
                        startTimer();
                        break;
                    case "stop_timer":
                        stopTimer();
                        break;
                    case "changed_current_participant":
                        setselectedParticipantStartNumber(message.message.id)
                        refusalToDisplay.current = message.message.refusals
                        faultsToDisplay.current = message.message.faults
                        if (message.message.started && !timerStaredRef.current) {
                            startTimer(message.message.time)
                        }
                        if (selectedRun.current !== message.message.currentRun) {
                            setreload(!reload)
                        }
                        selectedRun.current = message.message.currentRun

                        break;
                    case "changed_current_fault":
                        updateData()
                        break;
                    case "changed_current_refusal":
                        updateData()
                        break;
                    case "reload":
                        updateData()
                        break;
                }

            };


            ws.onerror = () => {
                closeWs()
                setwebsocket(null)
                ref.current = true;
                console.log("Error in Websocket")
            }

            ws.onclose = () => {
                closeWs()
                setwebsocket(null)
                ref.current = true;
                console.log("Websocket closed")
                setTimeout(() => {
                    setreload(!reload)
                }, 1000)
            }
            setwebsocket(ws);

            return () => {
                if (ws.readyState === WebSocket.OPEN) {
                    ws.close();
                    console.log("cleanup")
                }
            };

        } else {
            //Retry every 5 seconds to reconnect to the websocket, by setting the reload state
            setTimeout(() => {
                setreload(!reload)
            }, 5000)

        }

    }, [reload, params.date, params.organization, dispatch, changeFaults, changeRefusals, params.secret, updateData])


    useEffect(() => {
        return () => {
            if (websocket !== null && websocket.readyState === WebSocket.OPEN) {
                websocket.close();
                console.log("cleanup")
            }
        }
    }, [websocket])


    const startTimer = (initTime?: number) => {
        if (initTime !== undefined && !timerStaredRef.current) {
            setinitTime(initTime)
        }
        else {
            setinitTime(new Date().getTime());
        }
        setStarted(true)
        timerStaredRef.current = true
    }




    const stopTimer = () => {
        setStarted(false)
        timerStaredRef.current = false
    }



    const oldFaults = useRef(currentFaults)
    const oldRefusals = useRef(currentRefusals)
    const oldTime = useRef(currentTime)

    const oldPerson = useRef("-")
    const oldDog = useRef("-")

    const runnerDetails = useCallback(() => {
        if (unsafeselectedParticipant) {

            oldFaults.current = currentFaults
            oldRefusals.current = currentRefusals



            //update oldtime only if the new time is not -1
            if (getResultFromParticipant(currentRun, selectedParticipant).time > -1 || oldPerson.current !== selectedParticipant?.name) {
                oldTime.current = getResultFromParticipant(currentRun, selectedParticipant).time
                oldPerson.current = selectedParticipant?.name
                oldDog.current = selectedParticipant?.dog
            }

            if (selectedRun.current === currentRun) {
                return <Paper className={style.timeContainer}>
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
                                <Typography variant='h2'>{runTimeToStringClock(oldTime.current)}</Typography>
                            </Stack>
                            <Stack direction="row" gap={2} flexWrap="wrap" justifyContent="space-between">
                                <Typography variant='h2'>✋{faultsToDisplay.current}</Typography>
                                <Typography variant='h2'>✊{refusalToDisplay.current}</Typography>
                                <Typography variant='h2'>⌛{currentTimeFault}</Typography>

                            </Stack>
                        </Stack>


                    </Stack>
                </Paper>

            } else {
                return <Spacer vertical={0} />
            }
        } else {

            if (selectedRun.current === currentRun) {
                return <Paper className={style.timeContainer}>

                    <Stack gap={2} direction="column">
                        <Stack gap={1} direction="column" alignItems="flex-start">
                            <Typography variant='overline'>Aktuell am Start</Typography>
                            <Typography variant='h4'>{oldPerson.current}</Typography>
                            <Typography variant='h5'>mit</Typography>
                            <Typography variant='h4'>{oldDog.current}</Typography>
                        </Stack>
                        <Divider orientation='horizontal' flexItem />
                        <Stack direction="column" gap={2} alignItems="center" flexWrap="wrap">


                            <Stack direction="row"  >
                                <Typography variant='h2'>{runTimeToStringClock(oldTime.current)}</Typography>
                            </Stack>
                            <Stack direction="row" gap={2} flexWrap="wrap" justifyContent="space-between">
                                <Typography variant='h2'>✋{faultsToDisplay.current}</Typography>
                                <Typography variant='h2'>✊{refusalToDisplay.current}</Typography>
                                <Typography variant='h2'>⌛{currentTimeFault}</Typography>

                            </Stack>
                        </Stack>


                    </Stack>
                </Paper>
            } else {
                return <Spacer vertical={0} />
            }
        }

    }, [currentFaults, currentRefusals, currentRun, currentTimeFault, selectedParticipant, unsafeselectedParticipant])

    const resultTable = () => {
        if (!isKombi) {
            return getRanking(participantsWithResults, currentRun, calculatedStandardTime).map((value) => {
                return (
                    <TableRow key={value.participant.startNumber} className={value.participant.startNumber === selectedParticipant.startNumber &&
                        selectedRun.current === currentRun ? style.selected : ""}>
                        <TableCell>{value.rank > 0 ? `${value.rank}.` : ""}</TableCell>
                        <TableCell>{value.participant.name}</TableCell>
                        <TableCell>{value.participant.dog}</TableCell>
                        <TableCell>{runTimeToString(value.result.time)}</TableCell>
                        <TableCell>{value.result.time > 0 ? value.result.faults : "-"}</TableCell>
                        <TableCell>{value.result.time > 0 ? value.result.refusals : "-"}</TableCell>
                        <TableCell>{value.result.time > 0 ? getTimeFaults(value.result, calculatedStandardTime).toFixed(2) : "-"}</TableCell>
                    </TableRow>
                )
            })
        } else {
            const parcoursInfosA = common.runs.find(r => r.run === currentRunClass * 2 && r.height === currentSize)
            const parcoursInfosJ = common.runs.find(r => r.run === currentRunClass * 2 + 1 && r.height === currentSize)

            if (parcoursInfosA === undefined || parcoursInfosJ === undefined) {
                return <></>
            }

            const stdTimeA = standardTime(parcoursInfosA.run,
                currentSize,
                participants ? participants : [],
                parcoursInfosA?.length ? parcoursInfosA?.length : 0,
                parcoursInfosA?.speed ? parcoursInfosA?.speed : minSpeedA3)

            const stdTimeJ = standardTime(parcoursInfosJ.run,
                currentSize,
                participants ? participants : [],
                parcoursInfosJ?.length ? parcoursInfosJ?.length : 0,
                parcoursInfosJ?.speed ? parcoursInfosJ?.speed : minSpeedA3)

            const kombiRanking = getKombiRanking(participantsWithResults, currentRunClass, currentSize, stdTimeA, stdTimeJ)

            return kombiRanking.map((value) => {
                return (
                    <TableRow key={value.participant.startNumber} className={value.participant.startNumber === selectedParticipant.startNumber &&
                        selectedRun.current === currentRun ? style.selected : ""}>
                        <TableCell>{value.kombi > 0 ? `${value.kombi}.` : ""}</TableCell>
                        <TableCell>{value.participant.name}</TableCell>
                        <TableCell>{value.participant.dog}</TableCell>
                        <TableCell>{runTimeToString(value.totalTime)}</TableCell>
                        <TableCell>{value.totalFaults}</TableCell>
                    </TableRow>
                )
            })
        }
    }

    return (
        <Stack className={style.runContainer} direction="column" alignItems="center" gap={4}>
            {runnerDetails()}
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
                                    <TableCell align="center">Favorit</TableCell>
                                </TableRow>
                            </TableHead>
                            <TableBody>
                                {
                                    participants.sort((a, b) => a.sorting - b.sorting).map((p, index) => {
                                        return (
                                            <TableRow key={p.startNumber} className={p.startNumber === selectedParticipantStartNumber &&
                                                selectedRun.current === currentRun ? style.selected : ""}>
                                                <TableCell>{index + 1}</TableCell>
                                                <TableCell>{p.name} </TableCell>
                                                <TableCell>{p.dog}</TableCell>
                                                <TableCell align="center">
                                                    <Rating max={1} value={
                                                        checkIfFavorite(p, window.localStorage.getItem("favorites")) ? 1 : 0
                                                    }
                                                        onChange={(e, newValue) => {
                                                            let storage = window.localStorage.getItem("favorites")
                                                            storage = storage ? storage : ""

                                                            if (newValue === 1) {
                                                                storage = `${storage}${favoriteIdenfitier(p)};`
                                                            }
                                                            else {
                                                                storage = storage.replace(`${favoriteIdenfitier(p)};`, "")
                                                            }
                                                            setreload(!reload)
                                                            window.localStorage.setItem("favorites", storage)
                                                        }}
                                                    />
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
                        <Table >
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
                                {
                                    resultTable()
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