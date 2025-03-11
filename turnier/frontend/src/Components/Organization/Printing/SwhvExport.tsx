import React, { useMemo } from 'react'
import { heights, Participant, Run, RunInformation, runs, SkillLevel } from '../../../types/ResponseTypes'
import { classToString, getNumberOfParticipantsForRun, getParticipantsForRun, getRating, getRatingIndex, getTotalFaults, runClassToString, runToRunClass, standardTime } from '../../Common/StaticFunctionsTyped'
import { minSpeedA3 } from '../../Common/AgilityPO'
import { CommonReducerType } from '../../../Reducer/CommonReducer'
import { useSelector } from 'react-redux'
import { RootState } from '../../../Reducer/reducerCombiner'
import { useParams } from 'react-router-dom'
import { dateToURLString } from '../../Common/StaticFunctions'

type Props = {}

const SwhvExport = (props: Props) => {
    const params = useParams()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const date = useMemo(() => params.date ? new Date(params.date) : new Date(), [params.date])
    const turnament = common.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(date))

    const generateTable = () => {

        const participants = turnament?.participants ? turnament?.participants : []

        const normalRuns: Array<Array<number>> = []
        const normalRunsYouth: Array<number> = []
        const openRuns: Array<Array<number>> = []
        const openRunsYouth: Array<number> = []

        openRuns.push([0, 0, 0, 0, 0])
        openRuns.push([0, 0, 0, 0, 0])
        openRuns.push([0, 0, 0, 0, 0])
        openRuns.push([0, 0, 0, 0, 0])

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

            console.log(classToString(run!.run))

            const numberOfYouthParticipants = getNumberOfParticipantsForRun(participants, runToRunClass(run!.run), run!.height, true)
            const participantsForRun = getParticipantsForRun(participants, runToRunClass(run!.run), run!.height)

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

                openRuns[run.height][0] += ratings[0]
                openRuns[run.height][1] += ratings[1]
                openRuns[run.height][2] += ratings[2]
                openRuns[run.height][3] += ratings[3]
                openRuns[run.height][4] += ratings[4]

                openRunsYouth[run.height] += numberOfYouthParticipants
            }

        })

        const firstHalf = normalRuns.slice(0, 16)
        const secondHalf = normalRuns.slice(16, 32)

        const filler1 = Array(4 * 2).fill([0, 0, 0, 0, 0])

        let outputTable: number[][] = []
        outputTable = outputTable.concat(firstHalf)
        outputTable = outputTable.concat(filler1)
        outputTable = outputTable.concat(secondHalf)
        outputTable = outputTable.concat(filler1)
        outputTable = outputTable.concat(openRuns)
        outputTable = outputTable.concat(filler1)

        return outputTable.map((run) => {
            return <tr>
                {run.map((run) => <td>{run}</td>)}
            </tr>
        })

    }

    return (
        <div>
            <table>
                <tbody>
                    {generateTable()}
                </tbody>
            </table>
        </div>
    )
}

export default SwhvExport