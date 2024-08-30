import { Button, Checkbox, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle, FormControl, FormControlLabel, FormLabel, InputLabel, MenuItem, Paper, Radio, RadioGroup, Select, Stack } from '@mui/material'
import React, { useState } from 'react'
import { classToString, runClassToString, sizeToString } from '../../Common/StaticFunctionsTyped'
import { Participant, Result, Run, Size, SkillLevel } from '../../../types/ResponseTypes'
import Spacer from '../../Common/Spacer'
import style from './turnament.module.scss'
import { useDispatch } from 'react-redux'
import { addPrintResult } from '../../../Actions/SampleAction'
import { useNavigate } from 'react-router-dom'
import { ResultToPrint } from '../../../Reducer/CommonReducer'
import { el } from 'date-fns/locale'

export enum ListType {
    result,
    participant,
    sticker
}

type Props = {
    rankings: {
        run: Run;
        length: number | undefined;
        heights: {
            height: Size;
            stdTime: number;
            results: {
                result: Result;
                participant: Participant;
                rank: number;
                timefaults: number;
            }[];
        }[];
    }[],
    participants: Participant[],
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
                            <FormControlLabel value={ListType.participant} control={<Radio />} label="Starterliste(n)" />
                            <FormControlLabel value={ListType.result} control={<Radio />} label="Ergebnisliste(n)" />
                            <FormControlLabel value={ListType.sticker} control={<Radio />} label="Klebeliste(n)" />

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

                        } else if (listType === ListType.result) {
                            const toPrint = resultLists()
                            dispatch(addPrintResult(toPrint))
                            navigate("/o/hsf/2024-08-13/print")
                        } else if (listType === ListType.sticker) {

                        }

                        props.close()
                    }} variant='contained'>Generieren</Button>
                </Stack>
            </DialogActions>
        </Dialog >
    )
}

export default PrintingDialog