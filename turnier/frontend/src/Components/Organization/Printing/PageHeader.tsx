import style from './print.module.scss'
import { Stack, Typography } from '@mui/material'
import React from 'react'

type Props = {}

const PageHeader = (props: Props) => {
    return (
        <Stack direction="row" justifyContent="space-between">
            <Stack direction="column" alignItems="center" gap={1}>
                <img
                    src={`/Logo-Simple-black.svg`}
                    className={style.logo}
                    loading="eager"
                    alt="Logo"
                />
                <Typography align="center" className={style.text}>
                    DogDog Zeitmessung
                </Typography>
            </Stack >
            <Stack direction="column" alignItems="center" gap={1}>
                <Typography variant='h5' align="center" >
                    Sommerturnier - 05.06.2021
                </Typography>
                <Typography variant='h6' align="center" >
                    Neue Hundehalle e.V.
                </Typography>
                <Typography variant='h6' align="center" >
                    Richter: Supertoll
                </Typography>
            </Stack>
            <div className={style.logo}></div>
        </Stack >
    )
}

export default PageHeader