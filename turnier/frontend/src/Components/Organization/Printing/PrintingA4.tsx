import React from 'react'
import style from './print.module.scss'
import PageHeader from './PageHeader'
import { Button, IconButton, Paper, Stack, Typography } from '@mui/material'
import Spacer from '../../Common/Spacer'
import PrintIcon from '@mui/icons-material/Print';
import { useNavigate } from 'react-router-dom'
import ArrowBackIcon from '@mui/icons-material/ArrowBack';

type Props = {
    generationType: string,
    children?: React.JSX.Element[] | React.JSX.Element,
}

const PrintingA4 = (props: Props) => {
    const navigate = useNavigate()

    return <>
        <Paper className={style.printInvisibleHeader}>
            <Stack direction="row" gap={2} justifyContent="space-between">
                <IconButton onClick={() => navigate(-1)}>
                    <ArrowBackIcon />
                </IconButton>
                <Typography variant='h5'>{props.generationType} wurde(n) generiert</Typography>
                <Button variant='contained' onClick={() => window.print()}>
                    <PrintIcon />
                    <Spacer horizontal={5} />
                    Drucken
                </Button>
            </Stack>
        </Paper >

        <Stack className={style.a4Page} gap={2}>
            <PageHeader />
            {props.children}

        </Stack>
    </>


}

export default PrintingA4