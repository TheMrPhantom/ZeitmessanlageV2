import React from 'react'
import style from './print.module.scss'
import PageHeader from './PageHeader'
import PageType from './PrintType'
import { Stack, Table, TableCell, TableHead, TableRow } from '@mui/material'
import ParcoursInfo from './ParcoursInfo'
import { Table as TableType } from './Printing'
import { maximumTime, runTimeToString } from '../../Common/StaticFunctionsTyped'
import { ListType } from '../Turnament/PrintingDialog'
import { StickerInfo } from '../../../types/ResponseTypes'
import Sticker from './Sticker'


type Props = {
    tables?: TableType[],
    stickers?: StickerInfo[],
    type: ListType
}

const PrintingPage = (props: Props) => {

    const getEmojis = (table: TableType, place: number, timeFaults: number) => {
        let output = ""
        if (place === 1) {
            output += "🥇"
        } else if (place === 2) {
            output += "🥈"
        } else if (place === 3) {
            output += "🥉"
        }

        /*Get fastest time */
        let fastestTime = table.rows[0].result !== undefined ? table.rows[0].result.time : 0
        table.rows.forEach((row) => {

            if (row.result !== undefined && row.result.time < fastestTime && row.result.time > 0) {
                fastestTime = row.result.time
            }
        })
        /*
                if (table.rows[place - 1].result.time === fastestTime) {
                    output += "🚀"
                }
        */
        const row = table.rows[place - 1]
        /*First three places and no faults */
        if (place <= 3 && row.result !== undefined &&
            row.result.time > 0 &&
            row.result.faults === 0 &&
            row.result.refusals === 0 &&
            timeFaults < 1.0
        ) {
            output += "👑"
        }
        return output

    }

    if (props.type === ListType.result) {
        return (
            <>
                <Stack className={style.a4Page} gap={2}>
                    <PageHeader />

                    {props.tables?.map((element) => {
                        return <>
                            <div>
                                <PageType type={props.type} run={element.header.run} size={element.header.size} />
                                <ParcoursInfo parcoursLength={element.header.length}
                                    standardTime={element.header.standardTime}
                                    speed={element.header.length / element.header.standardTime}
                                    maxTime={maximumTime(element.header.run, element.header.standardTime)} />
                            </div>
                            <Table size="small">
                                <TableHead>
                                    <TableRow style={{ backgroundColor: '#dddddd' }}>
                                        <TableCell>#</TableCell>
                                        <TableCell>Name</TableCell>
                                        <TableCell>Hund</TableCell>
                                        <TableCell>Zeit</TableCell>
                                        <TableCell>F</TableCell>
                                        <TableCell>V</TableCell>
                                        <TableCell>ZF</TableCell>
                                        <TableCell></TableCell>
                                    </TableRow>
                                </TableHead>
                                {element.rows.map((row, index) => {
                                    if (row.result !== undefined && row.place !== undefined && row.timeFaults !== undefined) {
                                        return <TableRow style={{ backgroundColor: index % 2 === 1 ? '#efefef' : 'white' }}>
                                            <TableCell>{row.place}.</TableCell>
                                            <TableCell>{row.participant.name}</TableCell>
                                            <TableCell>{row.participant.dog}</TableCell>
                                            <TableCell>{runTimeToString(row.result.time)}</TableCell>
                                            <TableCell>{row.result.time > 0 ? row.result.faults : "-"}</TableCell>
                                            <TableCell>{row.result.time > 0 ? row.result.refusals : "-"}</TableCell>
                                            <TableCell>{row.result.time > 0 ? row.timeFaults : "-"}</TableCell>
                                            <TableCell>{getEmojis(element, row.place, row.timeFaults)}</TableCell>
                                        </TableRow>
                                    }
                                    return <></>
                                })}

                            </Table>
                        </>
                    })}
                </Stack>
            </>
        )
    } else if (props.type === ListType.participant) {
        return (
            <>
                <Stack className={style.a4Page} gap={2}>
                    <PageHeader />

                    {props.tables!.map((element) => {
                        return <>
                            <div>
                                <PageType type={props.type} run={element.header.run} size={element.header.size} />

                            </div>
                            <Table size="small">
                                <TableHead>
                                    <TableRow style={{ backgroundColor: '#dddddd' }}>
                                        <TableCell>#</TableCell>
                                        <TableCell>Starternummer</TableCell>
                                        <TableCell>Name</TableCell>
                                        <TableCell>Hund</TableCell>
                                        <TableCell>Verein</TableCell>
                                    </TableRow>
                                </TableHead>
                                {element.rows.map((row, index) => {
                                    return <TableRow style={{ backgroundColor: index % 2 === 1 ? '#efefef' : 'white' }}>

                                        <TableCell>{index + 1}</TableCell>
                                        <TableCell>{row.participant.startNumber}</TableCell>
                                        <TableCell>{row.participant.name}</TableCell>
                                        <TableCell>{row.participant.dog}</TableCell>
                                        <TableCell>{row.participant.club}</TableCell>

                                    </TableRow>
                                })}

                            </Table>
                        </>
                    })}
                </Stack>
            </>
        )
    } else if (props.type === ListType.sticker) {
        return <Stack className={style.a4Page}>{props.stickers!.map((element) => {
            if ((element.finalResult.resultA.time === -1 && element.finalResult.resultJ.time === -1) ||
                (element.finalResult.resultA.time === -2 && element.finalResult.resultJ.time === -2)) {
                return <></>
            }
            return <Sticker infos={element} />
        })}</Stack>
    }

    return <></>


}

export default PrintingPage