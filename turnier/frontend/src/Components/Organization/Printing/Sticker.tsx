import { Stack, Table, TableCell, TableHead, TableRow, Typography } from '@mui/material'
import React from 'react'
import style from './print.module.scss'
import CancelIcon from '@mui/icons-material/Cancel';
import { StickerInfo } from '../../../types/ResponseTypes';
import { dateToString } from '../../Common/StaticFunctions';
import { classToString, getRating, runTimeToString, sizeToString } from '../../Common/StaticFunctionsTyped';

type Props = {
    infos: StickerInfo
}

const Sticker = (props: Props) => {

    const totalFaultsA = props.infos.finalResult.resultA.faults +
        props.infos.finalResult.resultA.refusals +
        props.infos.finalResult.resultA.timefaults

    const totalFaultsJ = props.infos.finalResult.resultJ.faults +
        props.infos.finalResult.resultJ.refusals +
        props.infos.finalResult.resultJ.timefaults

    const timeA = props.infos.finalResult.resultA.time
    const timeJ = props.infos.finalResult.resultJ.time

    const ratingA = getRating(props.infos.finalResult.resultA.time, totalFaultsA)
    const ratingJ = getRating(props.infos.finalResult.resultJ.time, totalFaultsJ)

    const checkDis = (time: number, text: string | number) => {
        if (time > 0) {
            return text
        }
        return "-"
    }

    return (
        <Stack className={style.sticker} direction="row" flexWrap="nowrap" >
            <Stack className={style.turnamentInfo} direction="row" flexWrap="nowrap">
                <Stack style={{ padding: "3mm" }}>
                    <Typography fontWeight={900}>
                        {props.infos.participant.startNumber}
                    </Typography>
                </Stack>
                <Stack className={style.turnamentText}>
                    <Typography fontWeight={700}>{props.infos.turnament.name}</Typography>
                    <Typography>Neue Hundesporthalle e.V.</Typography>
                    <Typography>{dateToString(new Date(props.infos.turnament.date))}</Typography>
                    <Typography>dogdog-zeitmessung.de</Typography>
                </Stack>
            </Stack>

            <Stack className={style.tableContainer} >
                <Table size="small" className={style.stickerTable} sx={{ '& .MuiTableCell-root': { fontSize: '4mm' } }}>
                    <TableHead className={style.tableRow}>
                        <TableRow style={{ backgroundColor: '#dddddd' }} className={style.tableRow}>
                            <TableCell>Prüfung</TableCell>
                            <TableCell>Zeit</TableCell>
                            <TableCell>m/s</TableCell>
                            <TableCell>PF</TableCell>
                            <TableCell>V</TableCell>
                            <TableCell>ZF</TableCell>
                            <TableCell>GF</TableCell>
                            <TableCell>Wertung</TableCell>
                            <TableCell>Rang</TableCell>
                            <TableCell>Kombi</TableCell>
                        </TableRow>
                    </TableHead>
                    <TableRow className={style.tableRow}>
                        <TableCell className={style.tableRow}>{`FCI ${classToString(props.infos.finalResult.resultA.class)} ${sizeToString(props.infos.finalResult.resultA.size)}`}</TableCell>
                        <TableCell className={style.tableRow}>{runTimeToString(props.infos.finalResult.resultA.time)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, props.infos.finalResult.resultA.speed.toFixed(2))}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, props.infos.finalResult.resultA.faults)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, props.infos.finalResult.resultA.refusals)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, props.infos.finalResult.resultA.timefaults.toFixed(2))}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, totalFaultsA)}</TableCell>
                        <TableCell className={style.tableRow} style={{ backgroundColor: ratingA === "V0" ? '#dddddd' : '' }}>{ratingA}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeA, `${props.infos.finalResult.resultA.place}/${props.infos.finalResult.resultA.numberofparticipants}`)}</TableCell>
                        <TableCell className={style.tableRow} rowSpan={2} align="center">
                            {props.infos.finalResult.kombi.kombi > 0 ? `${props.infos.finalResult.kombi.kombi}/${props.infos.finalResult.resultA.numberofparticipants}` : <CancelIcon />}
                        </TableCell>
                    </TableRow>
                    <TableRow className={style.tableRow}>
                        <TableCell className={style.tableRow}>FCI J3 Intermediate</TableCell>
                        <TableCell className={style.tableRow}>{runTimeToString(props.infos.finalResult.resultJ.time)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, props.infos.finalResult.resultJ.speed.toFixed(2))}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, props.infos.finalResult.resultJ.faults)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, props.infos.finalResult.resultJ.refusals)}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, props.infos.finalResult.resultJ.timefaults.toFixed(2))}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, totalFaultsJ)}</TableCell>
                        <TableCell className={style.tableRow} style={{ backgroundColor: ratingJ === "V0" ? '#dddddd' : 'none' }}>{ratingJ}</TableCell>
                        <TableCell className={style.tableRow}>{checkDis(timeJ, `${props.infos.finalResult.resultJ.place}/${props.infos.finalResult.resultJ.numberofparticipants}`)}</TableCell>
                    </TableRow>

                </Table>
                <Typography className={style.starterText}> {`${props.infos.participant.name.split(" ")[0].toUpperCase()}, ${props.infos.participant.name.split(" ")[0]} - 0351308130 ${props.infos.participant.dog} (Lang ${props.infos.participant.dog})`}</Typography>
            </Stack>
            <Stack className={style.judgeContainer}>
                <Stack className={style.turnamentText}>
                    <Typography fontWeight={700}>Richter</Typography>
                    <Typography>{props.infos.turnament.judge}</Typography>
                </Stack>
            </Stack>
        </Stack >
    )
}

export default Sticker