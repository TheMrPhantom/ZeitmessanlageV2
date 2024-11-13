import React from 'react'
import PrintingPage from './PrintingPage'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useSelector } from 'react-redux';
import { RootState } from '../../../Reducer/reducerCombiner'
import { Participant, Result, Run, Size, StickerInfo } from '../../../types/ResponseTypes';
import { ListType } from '../Turnament/PrintingDialog';
import Spacer from '../../Common/Spacer';
import { Button, Paper, Stack, Typography } from '@mui/material';
import PrintIcon from '@mui/icons-material/Print';
import style from './print.module.scss'

type Props = {}

export type Header = { run: Run, size: Size, length: number, standardTime: number, listtype: ListType }
export type Row = { participant: Participant, result?: Result, timeFaults?: number, place?: number }
export type Footer = number
export type Page = Array<Table>
export type Table = { header: Header, rows: Array<Row> }

const Printing = (props: Props) => {
    const pageHeader = 35;
    const infoHeight = 20;
    const tableHeader = 10;
    const rowHeight = 10;
    const footerHeight = rowHeight;
    const maxheight = 297;
    const stickerHeight = 15;

    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    //const dispatch = useDispatch()



    const createPages = () => {
        let currentPageSize = pageHeader;
        const pages: Array<Page> = []
        let currentPage: Page = []
        const results = common.resultsToPrint
        const participants = common.participantspToPrint
        const sticker = common.stickersToPrint

        let currentTable: Table | null = null;
        let type = ListType.result
        const stickerLists: StickerInfo[][] = []

        if (results.length > 0) {
            type = ListType.result
            results.forEach((run) => {
                if (currentPageSize + infoHeight + tableHeader + footerHeight > maxheight) {
                    /*We want to create a new table but page is full*/
                    if (currentPage !== null) {
                        pages.push(currentPage)
                    }
                    currentPage = []
                    currentPageSize = 0
                    currentTable = { header: { ...run, listtype: ListType.result }, rows: [] }
                    currentPageSize += infoHeight + tableHeader

                } else {
                    /*We want to create a new table and have to space to do so*/

                    if (currentTable === null) {
                        currentTable = { header: { ...run, listtype: ListType.result }, rows: [] }
                    }

                    currentPageSize += infoHeight + tableHeader
                }
                run.results.forEach((result, index) => {
                    if (currentPageSize + rowHeight + footerHeight > maxheight) {
                        /*We want to create a new row but page is full*/
                        if (currentTable !== null) {
                            currentPage.push(currentTable)
                        }
                        pages.push(currentPage)

                        currentPage = []
                        currentPageSize = 0
                        //Create new Table

                        currentTable = { header: { ...run, listtype: ListType.result }, rows: [{ ...result, timeFaults: result.timeFaults, place: result.result.time > 0 ? index + 1 : undefined }] }
                        currentPageSize += infoHeight + tableHeader + rowHeight

                    } else {
                        /*We want to create a new row and have to space to do so*/
                        currentTable?.rows.push({ ...result, timeFaults: result.timeFaults, place: result.result.time > 0 ? index + 1 : undefined })
                        currentPageSize += rowHeight
                    }
                })
                if (currentTable !== null) {
                    currentPage.push(currentTable)
                }
                currentTable = null

            })
            pages.push(currentPage)
        } else if (participants.length > 0) {
            type = ListType.participant
            participants.forEach((participantsList) => {
                if (currentPageSize + infoHeight + tableHeader + footerHeight > maxheight) {
                    /*We want to create a new table but page is full*/
                    if (currentPage !== null) {
                        pages.push(currentPage)
                    }
                    currentPage = []
                    currentPageSize = 0
                    currentTable = {
                        header: {
                            ...participantsList,
                            listtype: ListType.participant,
                            length: 0,
                            standardTime: 0
                        }, rows: []
                    }
                    currentPageSize += infoHeight + tableHeader

                } else {
                    /*We want to create a new table and have to space to do so*/

                    if (currentTable === null) {
                        currentTable = {
                            header: {
                                ...participantsList,
                                listtype: ListType.result,
                                length: 0,
                                standardTime: 0
                            }, rows: []
                        }
                    }

                    currentPageSize += infoHeight + tableHeader
                }
                participantsList.participants.forEach((result, index) => {
                    if (currentPageSize + rowHeight + footerHeight > maxheight) {
                        /*We want to create a new row but page is full*/
                        if (currentTable !== null) {
                            currentPage.push(currentTable)
                        }
                        pages.push(currentPage)

                        currentPage = []
                        currentPageSize = 0
                        //Create new Table

                        currentTable = {
                            header: {
                                ...participantsList,
                                listtype: ListType.result,
                                length: 0,
                                standardTime: 0
                            }, rows: [{ participant: result }]
                        }
                        currentPageSize += infoHeight + tableHeader + rowHeight

                    } else {
                        /*We want to create a new row and have to space to do so*/
                        currentTable?.rows.push({ participant: result })
                        currentPageSize += rowHeight
                    }
                })
                if (currentTable !== null) {
                    currentPage.push(currentTable)
                }
                currentTable = null

            })
            pages.push(currentPage)
        } else if (sticker.length > 0) {
            let pageHeight = 0
            let stickerPage: StickerInfo[] = []

            sticker.forEach((stickerInfo) => {
                if (pageHeight + stickerHeight < maxheight) {
                    stickerPage.push(stickerInfo)
                    pageHeight += stickerHeight
                } else {
                    stickerLists.push(stickerPage)
                    stickerPage = []
                    pageHeight = 0
                }
            })
            if (stickerPage.length > 0) {
                stickerLists.push(stickerPage)
            }
        }

        //dispatch(clearPrints())

        if (pages.length > 0) {
            return pages.map((page) => <PrintingPage tables={page} type={type} />)
        } else if (sticker.length > 0) {
            return stickerLists.map((stickerPage) => <PrintingPage stickers={stickerPage} type={ListType.sticker} />)
        }


    }

    return (
        <>
            <Paper className={style.printInvisibleHeader}>
                <Stack direction="row" gap={2} justifyContent="space-between">
                    <Typography variant='h5'>Listen wurden generiert</Typography>
                    <Button variant='contained' onClick={() => window.print()}>
                        <PrintIcon />
                        <Spacer horizontal={5} />
                        Drucken
                    </Button>
                </Stack>
            </Paper >
            {createPages()}
        </>
    )
}

export default Printing