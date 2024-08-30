import { Stack, Typography } from '@mui/material'
import React from 'react'

type Props = {
    parcoursLength: number,
    standardTime: number,
    speed: number,
    maxTime: number
}

const ParcoursInfo = (props: Props) => {
    return (
        <Stack direction="row" justifyContent="space-around">
            <Typography>Parcourslänge: {props.parcoursLength}m</Typography>
            <Typography>Standardzeit: {props.standardTime.toFixed(2)}s</Typography>
            <Typography>Geschwindigkeit: {props.speed.toFixed(2)}m/s</Typography>
            <Typography>Maximalzeit: {props.maxTime.toFixed(2)}s</Typography>
        </Stack>
    )
}

export default ParcoursInfo