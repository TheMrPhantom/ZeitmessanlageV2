import React from 'react'
import { Paper, Table, TableBody, TableCell, TableContainer, TableHead, TableRow, Typography } from '@mui/material';
import style from './infobox.module.scss';

type Props = {
    headline: string,
    children: JSX.Element,
    noPadding?: boolean
}

const Infobox = (props: Props) => {
    return (
        <div className={props.noPadding ? style.tableContainerNoPadding : style.tableContainer}>
            <TableContainer component={Paper} className={style.table}>
                <Table aria-label="simple table">
                    <TableHead >
                        <TableRow >
                            <TableCell><Typography variant='h5'>{props.headline}</Typography></TableCell>
                        </TableRow>
                    </TableHead>
                    <TableBody >
                        <TableRow>
                            <TableCell component="th" scope="row">
                                {props.children}
                            </TableCell>
                        </TableRow>
                    </TableBody>
                </Table>
            </TableContainer>
        </div>
    )
}

export default Infobox