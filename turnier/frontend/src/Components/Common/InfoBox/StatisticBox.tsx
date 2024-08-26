import { Avatar, Typography } from '@mui/material'
import React from 'react'
import Infobox from './Infobox'
import style from './infobox.module.scss';



type Props = {
    headline: string,
    text: string,
    icon: JSX.Element
    colorCode?: string,
    noPadding?: boolean
}

const StatisticBox = (props: Props) => {
    return (
        <Infobox headline={props.headline} noPadding={props.noPadding} >
            <div className={style.statisticBox}>
                <Typography variant='h5'>{props.text}</Typography>
                <Avatar sx={{ bgcolor: props.colorCode ? props.colorCode : "#a9a9a9" }}>{props.icon}</Avatar>
            </div>
        </Infobox>
    )
}

export default StatisticBox