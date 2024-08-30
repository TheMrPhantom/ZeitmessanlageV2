import React, { useEffect, useState } from 'react'
import style from './print.module.scss'
import PageHeader from './PageHeader'
import PageType from './PrintType'
import { Stack, Table, TableCell, TableHead, TableRow } from '@mui/material'
import ParcoursInfo from './ParcoursInfo'
import { Table as TableType } from './Printing'
import { maximumTime, runTimeToString } from '../../Common/StaticFunctionsTyped'
import { ta } from 'date-fns/locale'


type Props = {
    tables: TableType[]
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
        let fastestTime = table.rows[0].result.time
        table.rows.forEach((row) => {
            if (row.result.time < fastestTime && row.result.time > 0) {
                fastestTime = row.result.time
            }
        })
        /*
                if (table.rows[place - 1].result.time === fastestTime) {
                    output += "🚀"
                }
        */
        /*First three places and no faults */
        if (place <= 3 &&
            table.rows[place - 1].result.time > 0 &&
            table.rows[place - 1].result.faults === 0 &&
            table.rows[place - 1].result.refusals === 0 &&
            timeFaults < 1.0
        ) {
            output += "👑"
        }
        return output

    }


    useEffect(() => {
        const delay = (ms: number) => {
            return new Promise(resolve => setTimeout(resolve, ms));
        }

        delay(1000).then(() => window.print());
    }, [])



    return (
        <>
            <Stack className={style.a4Page} gap={2}>
                <PageHeader />

                {props.tables.map((element) => {
                    return <>
                        <div>
                            <PageType type={'Ergebnisliste'} run={element.header.run} size={element.header.size} />
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
                            })}

                        </Table>
                    </>
                })}
            </Stack>
        </>
    )
}

export default PrintingPage