import { Button, Divider, Stack, ToggleButton, ToggleButtonGroup, Typography } from '@mui/material'
import React, { useEffect, useRef, useState } from 'react'
import style from './runselection.module.scss'
import CalendarMonthIcon from '@mui/icons-material/CalendarMonth';
import PetsIcon from '@mui/icons-material/Pets';
import Spacer from '../../Common/Spacer';
import { Participant, refreshIntervall, Size, SkillLevel, Tournament } from '../../../types/ResponseTypes';
import { useNavigate, useParams } from 'react-router-dom';
import Dog from './Dog';
import { dateToString } from '../../Common/StaticFunctions';
import { useDispatch, useSelector } from 'react-redux';
import { updateUserTurnament } from '../../../Actions/SampleAction';
import SportsIcon from '@mui/icons-material/Sports';
import { RootState } from '../../../Reducer/reducerCombiner'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import StadiumIcon from '@mui/icons-material/Stadium';
import { doGetRequest, favoriteIdenfitier, getParticipantsForRun, getTimeFaults, standardTime } from '../../Common/StaticFunctionsTyped';

type Props = {}

const RunSelection = (props: Props) => {
    const [skillLevel, setskillLevel] = useState(SkillLevel.A3)
    const [jumpHeight, setjumpHeight] = useState(Size.Small)
    const [reload, setreload] = useState(false)
    const [websocket, setwebsocket] = useState<WebSocket | null>(null);
    const [selectedParticipant, setselectedParticipant] = useState<{ id: number, faults: number, refusals: number, started: boolean, time: number, currentRun: number, currentSize: number }>(
        { id: -1, faults: 0, refusals: 0, started: false, time: 0, currentRun: -1, currentSize: -1 }
    )
    const params = useParams()
    const navigate = useNavigate()
    const dispatch = useDispatch()
    const [orgName, setorgName] = useState("")
    const [lastTimeReceivedRemaining, setlastTimeReceivedRemaining] = useState(new Date())
    const common: CommonReducerType = useSelector((state: RootState) => state.common);

    useEffect(() => {
        doGetRequest(`${params.organization}/tournament/${params.secret}/${params.date}`, dispatch).then((data) => {
            if (data.code === 200) {
                dispatch(updateUserTurnament(data.content as Tournament))
                settournamentCurrent(data.content as Tournament)
            }
        })
    }, [dispatch, reload, params.organization, params.date, params.secret])

    useEffect(() => {
        //Load org name
        doGetRequest(`organization/${params.organization}`, dispatch).then((data) => {
            if (data.code === 200) {
                setorgName(data.content.name)
            }
        })
    }, [params.organization, dispatch])


    const [tournamentCurrent, settournamentCurrent] = useState<Tournament>({ date: new Date(), judge: "", name: "", participants: [], runs: [] })
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
                console.log(message)
                switch (message.action) {
                    case "reload":
                        doGetRequest(`${params.organization}/tournament/${params.secret}/${params.date}`, dispatch).then((data) => {
                            if (data.code === 200) {
                                dispatch(updateUserTurnament(data.content as Tournament))
                                settournamentCurrent(data.content as Tournament)
                            }
                        })
                        break;
                    case "changed_current_participant":
                        setselectedParticipant(message.message)
                        setlastTimeReceivedRemaining(new Date())
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
                }
            };

        } else {
            //Retry every 5 seconds to reconnect to the websocket, by setting the reload state
            setTimeout(() => {
                setreload(!reload)
            }, 5000)

        }
    }, [reload, dispatch, params.date, params.organization, params.secret])


    useEffect(() => {
        return () => {
            if (websocket !== null && websocket.readyState === WebSocket.OPEN) {
                websocket.close();
                console.log("cleanup")
            }
        }
    }, [websocket])

    // Check every second if the last time received is older than 2 times the refresh intervall
    useEffect(() => {
        const interval = setInterval(() => {
            if (new Date().getTime() - lastTimeReceivedRemaining.getTime() > refreshIntervall * 2) {
                setselectedParticipant({ id: -1, faults: 0, refusals: 0, started: false, time: 0, currentRun: -1, currentSize: -1 })
            }
        }, 1000)
        return () => clearInterval(interval)
    }, [lastTimeReceivedRemaining])


    const favoriteDogs = () => {
        const favorites = window.localStorage.getItem('favorites')
        const favoriteParticipants: Participant[] = []
        common.userTurnament.participants.forEach((participant) => {
            if (favorites?.includes(participant.dog)) {
                favoriteParticipants.push(participant)
            }
        })

        if (favoriteParticipants.length === 0) {
            return <Typography variant="caption">Noch keine Hunde favorisiert</Typography>
        } else {
            return favoriteParticipants.map((participant) => {
                //Calculate standard time for the two runs
                const p = getParticipantsForRun(common.userTurnament.participants, participant.skillLevel, participant.size)
                let parcourLength = common.userTurnament.runs.find((run) => run.run === participant.skillLevel * 2)?.length
                let parcourSpeed = common.userTurnament.runs.find((run) => run.run === participant.skillLevel * 2)?.speed
                let stdTime = standardTime(participant.skillLevel * 2, participant.size, p, parcourLength ? parcourLength : 0, parcourSpeed ? parcourSpeed : 3)
                const timefaultsA = getTimeFaults(participant.resultA, stdTime)

                parcourLength = common.userTurnament.runs.find((run) => run.run === participant.skillLevel * 2 + 1)?.length
                parcourSpeed = common.userTurnament.runs.find((run) => run.run === participant.skillLevel * 2 + 1)?.speed
                stdTime = standardTime(participant.skillLevel * 2 + 1, participant.size, p, parcourLength ? parcourLength : 0, parcourSpeed ? parcourSpeed : 3)
                const timefaultsJ = getTimeFaults(participant.resultJ, stdTime)

                let dogsLeft = null

                // Is the Participant in the current run?
                if (selectedParticipant.id !== -1) {
                    if (participant.skillLevel === Math.floor(selectedParticipant.currentRun / 2)) {
                        console.log(participant.size, selectedParticipant.currentSize)
                        if (participant.size === selectedParticipant.currentSize) {
                            // Find selected participant in the list

                            const selectedParticipantObject = common.userTurnament.participants.find((p) => p.startNumber === selectedParticipant.id)

                            const allParticipantsOfRun = getParticipantsForRun(common.userTurnament.participants, participant.skillLevel, participant.size)

                            const selectedParticipantIndex = allParticipantsOfRun.findIndex((p) => p.startNumber === selectedParticipant.id)
                            const currentParticipantIndex = allParticipantsOfRun.findIndex((p) => p.startNumber === participant.startNumber)

                            if (selectedParticipantObject) {
                                dogsLeft = currentParticipantIndex - selectedParticipantIndex
                                if (dogsLeft < 0) {
                                    dogsLeft = null
                                }
                            }

                        }
                    }
                }

                return <Dog dogname={participant.dog}
                    resultA={participant.resultA}
                    resultJ={participant.resultJ}
                    timefaultsA={timefaultsA}
                    timefaultsJ={timefaultsJ}
                    dogsLeft={dogsLeft}
                    unlike={() => {
                        let storage = window.localStorage.getItem("favorites")
                        storage = storage ? storage : ""
                        storage = storage.replace(`${favoriteIdenfitier(participant)};`, "")
                        window.localStorage.setItem("favorites", storage)
                        setreload(!reload)
                    }}
                />
            })
        }
    }

    return (
        <Stack
            direction="column"
            className={style.container}
            gap={2}
        >
            <Spacer vertical={3} />
            <Stack direction="column" gap={2}>

                <Stack gap={1}>
                    <Typography variant="overline">Veranstaltungs Infos</Typography>
                    <Stack direction="row" gap={1}>
                        <PetsIcon />
                        <Typography variant="h6">{tournamentCurrent.name}</Typography>
                    </Stack>
                    <Stack direction="row" gap={1}>
                        <CalendarMonthIcon />
                        <Typography variant="h6">{dateToString(new Date(tournamentCurrent.date))}</Typography>
                    </Stack>
                    <Stack direction="row" gap={1}>
                        <StadiumIcon />
                        <Typography variant="h6">{orgName}</Typography>
                    </Stack>
                    <Stack direction="row" gap={1}>
                        <SportsIcon />
                        <Typography variant="h6">{tournamentCurrent.judge}</Typography>
                    </Stack>

                </Stack>
                <Divider />
                <Stack gap={1}>
                    <Typography variant="overline">Deine Hunde</Typography>
                    {favoriteDogs()}
                </Stack>

            </Stack>
            <Divider />
            <Stack direction="column" gap={1}>
                <Typography variant="overline">Lauf auswählen</Typography>
                <Stack gap={1}>
                    <ToggleButtonGroup
                        exclusive
                        value={skillLevel}
                        fullWidth
                        onChange={(e, value) => { if (value != null) { setskillLevel(value) } }}
                    >
                        <ToggleButton value={SkillLevel.A0}>
                            <Typography variant="overline">A0</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A1} >
                            <Typography variant="overline">A1</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A2} >
                            <Typography variant="overline">A2</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A3} >
                            <Typography variant="overline">A3</Typography>
                        </ToggleButton>
                    </ToggleButtonGroup>
                    <ToggleButtonGroup
                        exclusive
                        value={jumpHeight}
                        onChange={(e, value) => { if (value != null) { setjumpHeight(value) } }}
                        fullWidth
                    >
                        <ToggleButton value={Size.Small}>
                            <Typography variant="overline">Small</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Medium}>
                            <Typography variant="overline">Medium</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Intermediate} >
                            <Typography variant="overline">Intermediate</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Large} >
                            <Typography variant="overline">Large</Typography>
                        </ToggleButton>
                    </ToggleButtonGroup>
                    <Spacer vertical={5} />
                    <Stack direction="row" flexWrap="wrap" gap={3} justifyContent="space-evenly">
                        <Button variant='contained'
                            className={style.runButton}
                            onClick={() => {
                                navigate(`${skillLevel * 2}/${jumpHeight}`)
                            }}
                        >
                            Zum A Lauf
                        </Button>
                        <Button variant='contained'
                            className={style.runButton}
                            onClick={() => {
                                navigate(`${skillLevel * 2 + 1}/${jumpHeight}`)
                            }}
                        >
                            Zum Jumping
                        </Button>
                        <Button variant='contained'
                            className={style.runButton}
                            onClick={() => {
                                navigate(`${skillLevel * 2}/${jumpHeight}/kombi`)
                            }}
                        >
                            Zur Kombi
                        </Button>
                    </Stack>
                </Stack>


            </Stack>
        </Stack >

    )
}

export default RunSelection