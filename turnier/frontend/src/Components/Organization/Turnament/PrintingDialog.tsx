import { Button, Checkbox, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle, FormControl, FormControlLabel, FormLabel, InputLabel, MenuItem, Paper, Radio, RadioGroup, Select, Stack } from '@mui/material'
import React, { useState } from 'react'
import { classToString, getKombiRanking, runClassToString, sizeToString } from '../../Common/StaticFunctionsTyped'
import { ExtendedResult, FinalResult, KombiResult, Organization, Participant, Result, Run, Size, SkillLevel, StickerInfo, Turnament } from '../../../types/ResponseTypes'
import Spacer from '../../Common/Spacer'
import style from './turnament.module.scss'
import { useDispatch } from 'react-redux'
import { addPrintParticipant, addPrintResult, addPrintSticker } from '../../../Actions/SampleAction'
import { useNavigate } from 'react-router-dom'
import { ParticipantToPrint, ResultToPrint } from '../../../Reducer/CommonReducer'
import { el } from 'date-fns/locale'
import { assert } from 'console'
import { minSpeedA3, minSpeedJ3 } from '../../Common/AgilityPO'

export enum ListType {
    result,
    participant,
    sticker
}

export const listTypeToString = (type: ListType) => {
    switch (type) {
        case ListType.participant:
            return "Starterliste"
        case ListType.result:
            return "Ergebnisliste"
        case ListType.sticker:
            return "Klebeliste"
    }
}

type Props = {
    rankings: {
        run: Run;
        length: number | undefined;
        heights: {
            height: Size;
            stdTime: number;
            results: ExtendedResult[];
        }[];
    }[],
    participants: Participant[],
    organization: Organization,
    turnament: Turnament,
    isOpen: boolean,
    close: () => void
}

const PrintingDialog = (props: Props) => {
    const runs = [Run.A3, Run.J3, Run.A2, Run.J2, Run.A1, Run.J1, Run.A0, Run.J0]
    const heights = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
    const dispatch = useDispatch()
    const navigate = useNavigate()

    const runsAndHeights = runs.map((run) => {
        return {
            run: run, heights: heights.map((height) => {
                return { height: height, selected: false }
            })
        }
    })

    const [selectedRuns, setselectedRuns] = useState(runsAndHeights)

    const [listType, setlistType] = useState(ListType.participant)

    const isRunInterChecked = (run: Run) => {
        const check = selectedRuns.find((runAndHeight) => runAndHeight.run === run)?.heights.find((heights) => heights.selected === true)
        return check ? true : false
    }

    const isRunChecked = (run: Run) => {
        return selectedRuns.find((runAndHeight) => runAndHeight.run === run)?.heights.filter((heights) => heights.selected === true).length === heights.length
    }

    const resultLists = () => {
        const toPrint: ResultToPrint = []
        selectedRuns.forEach((runAndHeight) => {
            runAndHeight.heights.forEach((height) => {
                if (height.selected) {
                    const run = props.rankings.find((rank) => rank.run === runAndHeight.run)
                    if (run) {
                        const heightResults = run.heights.find((heights) => heights.height === height.height)
                        if (heightResults) {
                            toPrint.push({
                                run: run.run,
                                size: height.height,
                                results: heightResults.results.map((result) => {
                                    return {
                                        participant: result.participant,
                                        result: result.result,
                                        timeFaults: result.timefaults
                                    }
                                }),
                                length: run.length ? run.length : 0,
                                standardTime: heightResults.stdTime
                            })
                        }
                    }
                }
            })
        })
        return toPrint
    }

    const participantsList = () => {
        const toPrint: ParticipantToPrint = []
        selectedRuns.forEach((runAndHeight) => {
            runAndHeight.heights.forEach((height) => {
                if (height.selected) {
                    /*Get all participants for this run and height*/
                    const participants = props.participants.filter((participant) => participant.class === runAndHeight.run / 2 && participant.size === height.height).sort((a, b) => a.sorting - b.sorting)
                    toPrint.push({
                        run: runAndHeight.run,
                        size: height.height,
                        participants: participants
                    })
                }
            })
        })
        return toPrint
    }

    const stickerList = () => {

        /*Get a list of the selected classes and sizes*/
        const selectedClasses: SkillLevel[] = []
        const selectedSizes: Size[] = []

        selectedRuns.forEach((runAndHeight) => {
            runAndHeight.heights.forEach((height) => {
                if (height.selected) {
                    if (!selectedClasses.includes(runAndHeight.run / 2)) {
                        selectedClasses.push(runAndHeight.run / 2)
                    }
                    if (!selectedSizes.includes(height.height)) {
                        selectedSizes.push(height.height)
                    }
                }
            })
        })


        const rankings = props.rankings
        const toPrint: StickerInfo[] = []

        /*Get all participants for the selected classes and sizes*/
        const participants = props.participants.filter((participant) => selectedClasses.includes(participant.class) && selectedSizes.includes(participant.size))


        /* For each participant calculate the sticker info */
        participants.forEach((participant) => {
            const organization = props.organization
            const turnament = props.turnament
            const particpant = participant

            /*Get the rankings of the two runs*/
            const runA = rankings.find((rank) => rank.run === participant.class * 2)?.heights.find((height) => height.height === participant.size)?.results
            const runJ = rankings.find((rank) => rank.run === participant.class * 2 + 1)?.heights.find((height) => height.height === participant.size)?.results

            const resultA = runA?.find((result) => result.participant === participant)
            const resultJ = runJ?.find((result) => result.participant === participant)

            const placeA = resultA?.rank
            const placeJ = resultJ?.rank

            const size = participant.size

            /* Get parcours length of the two parcours */
            const lengthA = rankings.find((rank) => rank.run === participant.class * 2)?.length
            const lengthJ = rankings.find((rank) => rank.run === participant.class * 2 + 1)?.length

            /* Calculate speeds of the two runs */
            const speedA = lengthA ? lengthA / ((resultA?.result) ? (resultA?.result.time) : 1) : 0
            const speedJ = lengthJ ? lengthJ / ((resultJ?.result) ? (resultJ?.result.time) : 1) : 0

            /* Standard time of the two runs */
            const stdTimeA = rankings.find((rank) => rank.run === participant.class * 2)?.heights.find((height) => height.height === participant.size)?.stdTime
            const stdTimeJ = rankings.find((rank) => rank.run === participant.class * 2 + 1)?.heights.find((height) => height.height === participant.size)?.stdTime

            /* Timefaults of the two runs */
            const timeFaultsA = resultA?.timefaults
            const timeFaultsJ = resultJ?.timefaults

            /* Number of participants in the two runs */
            const numberOfParticipantsA = runA?.length
            const numberOfParticipantsJ = runJ?.length

            /* Get combined results */
            const kombiRankings = getKombiRanking(participants,
                participant.class,
                participant.size,
                stdTimeA ? stdTimeA : minSpeedA3,
                stdTimeJ ? stdTimeJ : minSpeedJ3)



            const kombiResult = kombiRankings.find((kombi) => kombi.participant === participant)

            /* Create the sticker info */
            const stickerInfo: StickerInfo = {
                organization: organization,
                turnament: turnament,
                participant: participant,
                finalResult: {
                    resultA: {
                        time: resultA?.result.time ? resultA?.result.time : 0,
                        faults: resultA?.result.faults ? resultA?.result.faults : 0,
                        refusals: resultA?.result.refusals ? resultA?.result.refusals : 0,
                        class: participant.class * 2,
                        place: placeA ? placeA : 0,
                        size: size,
                        speed: speedA,
                        timefaults: timeFaultsA ? timeFaultsA : 0,
                        numberofparticipants: numberOfParticipantsA ? numberOfParticipantsA : 0
                    },
                    resultJ: {
                        time: resultJ?.result.time ? resultJ?.result.time : 0,
                        faults: resultJ?.result.faults ? resultJ?.result.faults : 0,
                        refusals: resultJ?.result.refusals ? resultJ?.result.refusals : 0,
                        class: participant.class * 2 + 1,
                        place: placeJ ? placeJ : 0,
                        size: size,
                        speed: speedJ,
                        timefaults: timeFaultsJ ? timeFaultsJ : 0,
                        numberofparticipants: numberOfParticipantsJ ? numberOfParticipantsJ : 0
                    },
                    kombi: kombiResult ? kombiResult : {
                        participant: participant,
                        totalFaults: -1,
                        totalTime: -1,
                        kombi: -1
                    }
                }
            }

            toPrint.push(stickerInfo)
        })

        /* Sort stickerlist by startnumber */
        toPrint.sort((a, b) => a.participant.startNumber - b.participant.startNumber)

        return toPrint
    }

    return (
        <Dialog open={props.isOpen} onClose={props.close} sx={{ zIndex: 20000000 }} >
            <DialogTitle>Listen drucken</DialogTitle>
            <DialogContent >
                <DialogContentText>
                    Welche Listen für welche läufe möchtest du drucken?
                </DialogContentText>
                <Spacer vertical={20} />
                <Stack direction="column" gap={2} flexWrap="wrap" >
                    <FormControl>
                        <FormLabel>Listenart</FormLabel>
                        <RadioGroup
                            row
                            value={listType}
                            onChange={(e) => { setlistType(parseInt(e.target.value)) }}
                        >
                            <FormControlLabel value={ListType.participant} control={<Radio />} label={listTypeToString(ListType.participant)} />
                            <FormControlLabel value={ListType.result} control={<Radio />} label={listTypeToString(ListType.result)} />
                            <FormControlLabel value={ListType.sticker} control={<Radio />} label={listTypeToString(ListType.sticker)} />

                        </RadioGroup>
                    </FormControl>
                    <Stack direction="row" flexWrap="wrap" gap={3} justifyContent="space-evenly">
                        <Button variant='outlined' onClick={() => {
                            const newRuns = selectedRuns.map((runAndHeight) => {
                                const newHeights = runAndHeight.heights.map((heights) => {
                                    return { height: heights.height, selected: false }
                                })
                                return { run: runAndHeight.run, heights: newHeights }
                            })
                            setselectedRuns(newRuns)
                        }}>Keine auswählen</Button>
                        <Button variant='outlined' onClick={() => {
                            const newRuns = selectedRuns.map((runAndHeight) => {
                                const newHeights = runAndHeight.heights.map((heights) => {
                                    return { height: heights.height, selected: true }
                                })
                                return { run: runAndHeight.run, heights: newHeights }
                            })
                            setselectedRuns(newRuns)
                        }}>Alle auswählen</Button>
                    </Stack>
                    <Stack direction="row" gap={2} flexWrap="wrap" >
                        {runs.map((run) => {
                            return (
                                <Paper className={style.padding} elevation={5}>
                                    <Stack direction="column">
                                        <FormControlLabel
                                            label={classToString(run)}
                                            control={
                                                <Checkbox
                                                    checked={isRunChecked(run)}
                                                    indeterminate={isRunInterChecked(run) && !isRunChecked(run)}
                                                    onChange={() => {
                                                        /*If not checked check all, if inter checked check all, if all checked uncheck all*/
                                                        const newRuns = selectedRuns.map((runAndHeight) => {
                                                            if (runAndHeight.run === run) {
                                                                const newHeights = runAndHeight.heights.map((heights) => {
                                                                    if (isRunChecked(run)) {
                                                                        return { height: heights.height, selected: false }
                                                                    } else {
                                                                        return { height: heights.height, selected: true }
                                                                    }
                                                                })
                                                                return { run: run, heights: newHeights }
                                                            } else {
                                                                return runAndHeight
                                                            }
                                                        })
                                                        setselectedRuns(newRuns)
                                                    }}
                                                />
                                            }
                                        />
                                        {heights.map((height) => {
                                            return (
                                                <Stack direction="row">
                                                    <Spacer horizontal={20} />
                                                    <FormControlLabel
                                                        label={sizeToString(height)}
                                                        control={<Checkbox checked={
                                                            selectedRuns.find((runAndHeight) => runAndHeight.run === run)?.heights.find((heights) => heights.height === height)?.selected
                                                        }
                                                            onChange={(value) => {
                                                                const newRuns = selectedRuns.map((runAndHeight) => {
                                                                    if (runAndHeight.run === run) {
                                                                        const newHeights = runAndHeight.heights.map((heights) => {
                                                                            if (heights.height === height) {
                                                                                return { height: heights.height, selected: value.target.checked }
                                                                            } else {
                                                                                return heights
                                                                            }
                                                                        })
                                                                        return { run: run, heights: newHeights }
                                                                    } else {
                                                                        return runAndHeight
                                                                    }
                                                                })
                                                                setselectedRuns(newRuns)
                                                            }}
                                                        />}
                                                    />
                                                </Stack>
                                            )
                                        })}
                                    </Stack >
                                </Paper>
                            )
                        }

                        )}
                    </Stack>
                </Stack>
            </DialogContent>
            <DialogActions>
                <Stack direction="row" justifyContent="space-between" className={style.dialogButtons}>
                    <Button onClick={props.close} variant='outlined'>Abbrechen</Button>
                    <Button onClick={() => {
                        if (listType === ListType.participant) {
                            const toPrint = participantsList()
                            dispatch(addPrintParticipant(toPrint))
                            navigate("/o/hsf/2024-08-13/print")
                        } else if (listType === ListType.result) {
                            const toPrint = resultLists()
                            dispatch(addPrintResult(toPrint))
                            navigate("/o/hsf/2024-08-13/print")
                        } else if (listType === ListType.sticker) {
                            const toPrint = stickerList()
                            dispatch(addPrintSticker(toPrint))
                            navigate("/o/hsf/2024-08-13/print")
                        }

                        props.close()
                    }} variant='contained'>Generieren</Button>
                </Stack>
            </DialogActions>
        </Dialog >
    )
}

export default PrintingDialog