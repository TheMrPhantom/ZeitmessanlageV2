import { Stack, Typography } from '@mui/material'
import React from 'react'
import style from './print.module.scss'
import { Run, Size } from '../../../types/ResponseTypes'
import { classToString, runClassToString, sizeToString } from '../../Common/StaticFunctionsTyped'

type Props = {
    type: "Ergebnisliste" | "Startliste",
    run: Run,
    size: Size
}

const PageType = (props: Props) => {
    return (
        <Stack className={style.pagetypeContainer} justifyContent="center" alignItems="center">
            <Typography variant='h5'> {props.type} {classToString(props.run)} - {sizeToString(props.size)}</Typography>
        </Stack>
    )
}

export default PageType