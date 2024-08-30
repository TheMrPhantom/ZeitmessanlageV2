import React from 'react'
import PrintingPage from './PrintingPage'
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { useDispatch, useSelector } from 'react-redux';
import { RootState } from '../../../Reducer/reducerCombiner'
import { Participant, Result, Run, Size } from '../../../types/ResponseTypes';

type Props = {}

export type Header = { run: Run, size: Size, length: number, standardTime: number }
export type Row = { participant: Participant, result: Result, timeFaults: number, place: number }
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

    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const dispatch = useDispatch()

    const pages = []

    const createPages = () => {
        let currentPageSize = pageHeader;
        const pages: Array<Page> = []
        let currentPage: Page = []
        const results = common.resultsToPrint
        let currentTable: Table | null = null;
        console.log(results)
        results.forEach((run) => {
            if (currentPageSize + infoHeight + tableHeader + footerHeight > maxheight) {
                /*We want to create a new table but page is full*/
                if (currentPage !== null) {
                    pages.push(currentPage)
                }
                currentPage = []
                currentPageSize = 0
                currentTable = { header: run, rows: [] }
                currentPageSize += infoHeight + tableHeader

            } else {
                /*We want to create a new table and have to space to do so*/

                if (currentTable === null) {
                    currentTable = { header: run, rows: [] }
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

                    currentTable = { header: run, rows: [{ ...result, timeFaults: result.timeFaults, place: index + 1 }] }
                    currentPageSize += infoHeight + tableHeader + rowHeight

                } else {
                    /*We want to create a new row and have to space to do so*/
                    currentTable?.rows.push({ ...result, timeFaults: result.timeFaults, place: index + 1 })
                    currentPageSize += rowHeight
                }
            })
            if (currentTable !== null) {
                currentPage.push(currentTable)
            }
            console.log(currentPage)
            currentTable = null

        })
        pages.push(currentPage)

        console.log(pages)
        return pages.map((page) => <PrintingPage tables={page} />)

    }

    return (
        <>{createPages()}</>
    )
}

export default Printing