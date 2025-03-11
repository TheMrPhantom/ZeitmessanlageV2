import React, { useCallback, useEffect, useMemo, useState } from 'react'
import { RunInformation } from '../../../types/ResponseTypes'
import { classToString, getNumberOfParticipantsForRun, getParticipantsForRun, getRating, getRatingIndex, getTotalFaults, runToRunClass, standardTime } from '../../Common/StaticFunctionsTyped'
import { minSpeedA3 } from '../../Common/AgilityPO'
import { CommonReducerType } from '../../../Reducer/CommonReducer'
import { useDispatch, useSelector } from 'react-redux'
import { RootState } from '../../../Reducer/reducerCombiner'
import { useNavigate, useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions'
import { openToast } from '../../../Actions/CommonAction'
import { Button, Paper, Stack, Step, StepLabel, Stepper, Typography } from '@mui/material'
import style from './print.module.scss'
import Spacer from '../../Common/Spacer'

type Props = {}

const SwhvExport = (props: Props) => {
    const params = useParams()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const date = useMemo(() => params.date ? new Date(params.date) : new Date(), [params.date])
    const turnament = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))
    const dispatch = useDispatch()
    const navigate = useNavigate()
    const [tables, settables] = useState<{ results: number[][], youth: number[][] }>({ results: [], youth: [] })
    const [currentStep, setcurrentStep] = useState(0)

    const generateTables = useCallback(() => {

        const participants = turnament?.participants ? turnament?.participants : []

        const normalRuns: Array<Array<number>> = []
        const normalRunsYouth: Array<number> = []
        const openRuns: Array<Array<number>> = Array(2 * 4).fill([0, 0, 0, 0, 0])
        const openRunsYouth: Array<number> = Array(2 * 4).fill(0)


        const runs = turnament?.runs ? turnament?.runs : []


        //Check if all runs are not undefined
        if (runs.some(r => r === undefined)) {
            return
        }

        let runsSorted: RunInformation[] = []

        runsSorted = runsSorted.concat(runs.slice(4 * 0, 4 * 1))
        runsSorted = runsSorted.concat(runs.slice(4 * 2, 4 * 3))
        runsSorted = runsSorted.concat(runs.slice(4 * 4, 4 * 5))
        runsSorted = runsSorted.concat(runs.slice(4 * 6, 4 * 7))

        runsSorted = runsSorted.concat(runs.slice(4 * 1, 4 * 2))
        runsSorted = runsSorted.concat(runs.slice(4 * 3, 4 * 4))
        runsSorted = runsSorted.concat(runs.slice(4 * 5, 4 * 6))
        runsSorted = runsSorted.concat(runs.slice(4 * 7, 4 * 8))

        runsSorted.forEach(run => {

            if (classToString(run!.run) === undefined) {
                return
            }


            const numberOfYouthParticipants = getNumberOfParticipantsForRun(participants, runToRunClass(run!.run), run!.height, true)
            const participantsForRun = getParticipantsForRun(participants, runToRunClass(run!.run), run!.height)
            console.log(numberOfYouthParticipants, classToString(run!.run), run!.height, participantsForRun)
            const length = run!.length
            const speed = run!.speed
            const stdTime = standardTime(run!.run, run!.height, participantsForRun, length, speed ? speed : minSpeedA3)
            const ratings = [0, 0, 0, 0, 0]

            participantsForRun.forEach(p => {
                const totalFaults = getTotalFaults(run!.run % 2 === 0 ? p.resultA : p.resultJ, stdTime)
                const rating = getRating(run!.run % 2 === 0 ? p.resultA.time : p.resultJ.time, totalFaults)
                ratings[getRatingIndex(rating)]++
            })

            if (!run.isGame) {
                normalRuns.push(ratings)
                normalRunsYouth.push(numberOfYouthParticipants)

            } else {
                normalRuns.push([0, 0, 0, 0, 0])
                normalRunsYouth.push(0)

                const isAgilityRun = run.run % 2 === 0

                //is agility run add the first 4 rows of the open runs if not the last 4
                if (isAgilityRun) {
                    openRuns[run.height] = ratings
                    openRunsYouth[run.height] = numberOfYouthParticipants
                } else {
                    openRuns[run.height + 4] = ratings
                    openRunsYouth[run.height + 4] = numberOfYouthParticipants
                }

            }

        })

        const firstHalf = normalRuns.slice(0, 16)
        const secondHalf = normalRuns.slice(16, 32)

        const firstHalfYouth = normalRunsYouth.slice(0, 16)
        const secondHalfYouth = normalRunsYouth.slice(16, 32)

        const filler1 = Array(4 * 2).fill([0, 0, 0, 0, 0])
        const filler2 = Array(4 * 1).fill([0, 0, 0, 0, 0])

        const filler1Youth = Array(4 * 2).fill(0)
        const filler2Youth = Array(4 * 1).fill(0)



        let outputTable: number[][] = []
        outputTable = outputTable.concat(firstHalf)
        outputTable = outputTable.concat(filler1)
        outputTable = outputTable.concat(secondHalf)
        outputTable = outputTable.concat(filler1)
        outputTable = outputTable.concat(openRuns)
        outputTable = outputTable.concat(filler2)

        let outputTableYouth: number[] = []
        outputTableYouth = outputTableYouth.concat(firstHalfYouth)
        outputTableYouth = outputTableYouth.concat(filler1Youth)
        outputTableYouth = outputTableYouth.concat(secondHalfYouth)
        outputTableYouth = outputTableYouth.concat(filler1Youth)
        outputTableYouth = outputTableYouth.concat(openRunsYouth)
        outputTableYouth = outputTableYouth.concat(filler2Youth)

        return { results: outputTable, youth: outputTableYouth.map((v) => [v]) }

    }, [turnament?.participants, turnament?.runs])

    const copyTableAsText = (outputTable: number[][]) => {
        let text = outputTable.map(row => row.join("\t")).join("\n"); // Convert array to tab-separated text

        navigator.clipboard.writeText(text).then(() => {
            dispatch(openToast({ message: "Tabelle kopiert" }))
        }).catch(err => {
            dispatch(openToast({ message: "Fehler beim Kopieren der Tabelle", type: "error" }))
        });
    }

    useEffect(() => {
        const t = generateTables()
        if (t) {
            settables(t)
            setcurrentStep(1)
        }
    }, [turnament, generateTables])

    const stepperContent = () => {
        if (currentStep === 1) {
            return <Stack gap={2} className={style.stepStack} alignItems={"center"}>
                <Typography variant='h6'>Formular des swhv herunterladen</Typography >
                <Typography>Lade mit dem unten stehenden Knopf die Excel Tabelle des swhv herunter</Typography>
                <Button onClick={() => window.open("https://swhv.de/fileadmin/swhv.de/Dokumente/Formulare_und_Texte/Agility/swhv_Agility_Statistikformular_2025_neu.xls", "_blank")} variant='contained'>
                    Herunterladen
                </Button>

                <Button onClick={() => setcurrentStep(currentStep + 1)} variant='contained'>
                    Alles klar, ich habe die Excel heruntergeladen
                </Button>
            </Stack>
        } else if (currentStep === 2) {
            return <Stack gap={2} className={style.stepStack} alignItems={"center"}>
                <Typography variant='h6'>Starter in Excel eintragen</Typography >
                <Typography>Kopiere mit dem unten stehenden Knopf die Starterinformationen, und füge sie in die Heruntergeladene Excel Tabelle ein.</Typography>
                <Button onClick={() => copyTableAsText(tables.results)} variant='contained'>
                    Ergebnisse kopieren
                </Button>
                <Typography>Nach dem Kopieren, klicke in die im Bild markierte Zelle in Excel. Über Rechtsklick und dann 'Einfügen' fügst du die Daten ein.</Typography>
                <img src="/copy-starter.png" alt="Excel Tabelle" style={{ maxWidth: "400px", width: "100%" }} />
                <Spacer vertical={2} />
                <Button onClick={() => setcurrentStep(currentStep + 1)} variant='contained'>
                    Alles klar, ich habe die Daten eingefügt
                </Button>
            </Stack>
        } else if (currentStep === 3) {
            return <Stack gap={2} className={style.stepStack} alignItems={"center"}>
                <Typography variant='h6'>Jugendstarter in Excel eintragen</Typography >
                <Typography>Kopiere mit dem unten stehenden Knopf die Jugend-Starterinformationen, und füge sie in die Heruntergeladene Excel Tabelle ein.</Typography>
                <Button onClick={() => copyTableAsText(tables.youth)} variant='contained'>
                    Jugendstarter kopieren
                </Button>
                <Typography>Nach dem Kopieren, klicke in die im Bild markierte Zelle in Excel. Über Rechtsklick und dann 'Einfügen' fügst du die Daten ein.</Typography>
                <img src="/copy-youth.png" alt="Excel Tabelle" style={{ maxWidth: "500px", width: "100%" }} />
                <Spacer vertical={2} />
                <Button onClick={() => setcurrentStep(currentStep + 1)} variant='contained'>
                    Alles klar, ich habe die Daten eingefügt
                </Button>
            </Stack>
        } else if (currentStep === 4) {
            return <Stack gap={2} className={style.stepStack} alignItems={"center"}>
                <Typography variant='h6'>Alles erledigt!</Typography >
                <Typography>Du kannst nun zurück zur Übersichtsseite</Typography>
                <Button onClick={() => navigate("../../", { relative: "path" })} variant='contained'>
                    Zurück zur Übersichtsseite
                </Button>

            </Stack>
        }
    }

    return (
        <>
            <Stack className={style.container} gap={2}>

                <Typography variant="h4">swhv Export</Typography>
                <Stepper orientation={window.innerWidth < window.globalTS.MOBILE_THRESHOLD ? 'vertical' : 'horizontal'} activeStep={currentStep}>
                    <Step>
                        <StepLabel>Listen generieren</StepLabel>
                    </Step>
                    <Step>
                        <StepLabel>Formular herunterladen</StepLabel>
                    </Step>
                    <Step>
                        <StepLabel>Ergebnisse kopieren</StepLabel>
                    </Step>
                    <Step>
                        <StepLabel>Jugenstarter kopieren</StepLabel>
                    </Step>
                    <Step>
                        <StepLabel>Fertig</StepLabel>
                    </Step>
                </Stepper>
                <Stack flexDirection={"column"} gap={2} alignItems={"center"}>
                    <Paper style={{}}>
                        {stepperContent()}
                    </Paper>
                </Stack>
            </Stack>
        </>
    )
}

export default SwhvExport